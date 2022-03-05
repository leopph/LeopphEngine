#include "ShaderProgram.hpp"

#include "../../util/Logger.hpp"
#include "../opengl/OpenGl.hpp"

#include <glad/gl.h>

#include <stdexcept>
#include <vector>


namespace leopph::internal
{
	ShaderProgram::ShaderProgram(const std::vector<ShaderStageInfo>& stageInfo) :
		m_ProgramName{glCreateProgram()}
	{
		std::vector<unsigned> shaderNames;

		std::ranges::for_each(stageInfo, [&](const auto& info)
		{
			const auto shaderName{glCreateShader(opengl::TranslateShaderType(info.Type))};
			glShaderSource(shaderName, 1, std::array{info.Src.data()}.data(), nullptr);
			glCompileShader(shaderName);

			if (const auto status{CompilationStatus(shaderName)};
				status.second.has_value())
			{
				if (status.first)
				{
					Logger::Instance().Debug(status.second.value());
				}
				else
				{
					Logger::Instance().Error(status.second.value());
					glDeleteShader(shaderName);
					return;
				}
			}

			shaderNames.push_back(shaderName);
			glAttachShader(m_ProgramName, shaderName);
		});

		glLinkProgram(m_ProgramName);

		if (const auto status{LinkStatus()};
			status.second.has_value())
		{
			if (status.first)
			{
				Logger::Instance().Debug(status.second.value());
			}
			else
			{
				Logger::Instance().Error(status.second.value());
			}
		}

		std::ranges::for_each(shaderNames, [](const auto& shaderName)
		{
			glDeleteShader(shaderName);
		});
	}

	ShaderProgram::ShaderProgram(const std::span<const unsigned char> binary) :
		m_ProgramName{glCreateProgram()}
	{
		// Try all the formats, return after a successful link
		for (const auto format : opengl::ShaderBinaryFormats())
		{
			// use .data() because elemenets weren't pushed back into the vector
			// so it sees itself as empty
			glProgramBinary(m_ProgramName, format, binary.data(), binary.size_bytes());

			// glProgramBinary sets GL_LINK_STATUS to GL_TRUE on successful upload
			GLint success;
			glGetProgramiv(m_ProgramName, GL_LINK_STATUS, &success);
			if (success == GL_TRUE)
			{
				return;
			}
		}

		// We failed the upload, clean up and throw
		glDeleteProgram(m_ProgramName);
		throw std::runtime_error{"Couldn't upload shader binary data."};
	}

	ShaderProgram::~ShaderProgram() noexcept
	{
		glDeleteProgram(m_ProgramName);
	}

	auto ShaderProgram::Use() const -> void
	{
		glUseProgram(m_ProgramName);
	}

	auto ShaderProgram::SetUniform(const std::string_view name, const bool value) -> void
	{
		glProgramUniform1i(m_ProgramName, GetUniformLocation(name), value);
	}

	auto ShaderProgram::SetUniform(const std::string_view name, const int value) -> void
	{
		glProgramUniform1i(m_ProgramName, GetUniformLocation(name), value);
	}

	auto ShaderProgram::SetUniform(const std::string_view name, const unsigned value) -> void
	{
		glProgramUniform1ui(m_ProgramName, GetUniformLocation(name), value);
	}

	auto ShaderProgram::SetUniform(const std::string_view name, const float value) -> void
	{
		glProgramUniform1f(m_ProgramName, GetUniformLocation(name), value);
	}

	auto ShaderProgram::SetUniform(const std::string_view name, const Vector3& value) -> void
	{
		glProgramUniform3fv(m_ProgramName, GetUniformLocation(name), 1, value.Data().data());
	}

	auto ShaderProgram::SetUniform(const std::string_view name, const Matrix4& value) -> void
	{
		glProgramUniformMatrix4fv(m_ProgramName, GetUniformLocation(name), 1, GL_TRUE, reinterpret_cast<const GLfloat*>(value.Data().data()));
	}

	auto ShaderProgram::SetUniform(const std::string_view name, const std::span<const int> values) -> void
	{
		glProgramUniform1iv(m_ProgramName, GetUniformLocation(name), static_cast<GLsizei>(values.size()), values.data());
	}

	auto ShaderProgram::SetUniform(const std::string_view name, const std::span<const unsigned> values) -> void
	{
		glProgramUniform1uiv(m_ProgramName, GetUniformLocation(name), static_cast<GLsizei>(values.size()), values.data());
	}

	auto ShaderProgram::SetUniform(const std::string_view name, const std::span<const float> values) -> void
	{
		glProgramUniform1fv(m_ProgramName, GetUniformLocation(name), static_cast<GLsizei>(values.size()), values.data());
	}

	auto ShaderProgram::SetUniform(const std::string_view name, const std::span<const Vector3> values) -> void
	{
		glProgramUniform3fv(m_ProgramName, GetUniformLocation(name), static_cast<GLsizei>(values.size()), reinterpret_cast<const GLfloat*>(values.data()));
	}

	auto ShaderProgram::SetUniform(const std::string_view name, const std::span<const Matrix4> values) -> void
	{
		glProgramUniformMatrix4fv(m_ProgramName, GetUniformLocation(name), static_cast<GLsizei>(values.size()), GL_TRUE, reinterpret_cast<const GLfloat*>(values.data()));
	}

	auto ShaderProgram::SetBufferBinding(const std::string_view bufName, const int bindingIndex) -> void
	{ }


	auto ShaderProgram::Binary() const -> std::vector<unsigned char>
	{
		const auto binSz{[this]
		{
			GLint ret;
			glGetProgramiv(m_ProgramName, GL_PROGRAM_BINARY_LENGTH, &ret);
			return ret;
		}()};
		std::vector<unsigned char> binary(binSz);
		GLenum format;
		glGetProgramBinary(m_ProgramName, binary.size(), nullptr, &format, binary.data());
		return binary;
	}


	auto ShaderProgram::CompilationStatus(const unsigned name) -> std::pair<bool, std::optional<std::string>>
	{
		std::pair<bool, std::optional<std::string>> ret;

		GLint logLength;
		glGetShaderiv(name, GL_INFO_LOG_LENGTH, &logLength);

		if (logLength > 0)
		{
			std::string infoLog;
			infoLog.resize(logLength);
			glGetShaderInfoLog(name, logLength, &logLength, infoLog.data());
			ret.second = infoLog;
		}

		GLint status;
		glGetShaderiv(name, GL_COMPILE_STATUS, &status);
		ret.first = status == GL_TRUE;
		return ret;
	}

	auto ShaderProgram::LinkStatus() const -> std::pair<bool, std::optional<std::string>>
	{
		std::pair<bool, std::optional<std::string>> ret;

		GLint logLength;
		glGetProgramiv(m_ProgramName, GL_INFO_LOG_LENGTH, &logLength);

		if (logLength > 0)
		{
			std::string infoLog;
			infoLog.resize(logLength);
			glGetProgramInfoLog(m_ProgramName, logLength, &logLength, infoLog.data());
			ret.second = infoLog;
		}

		GLint status;
		glGetProgramiv(m_ProgramName, GL_LINK_STATUS, &status);
		ret.first = status == GL_TRUE;
		return ret;
	}

	auto ShaderProgram::GetUniformLocation(const std::string_view name) -> int
	{
		auto it{m_UniformLocations.find(name)};

		if (it == m_UniformLocations.end())
		{
			it = m_UniformLocations.emplace(name, glGetUniformLocation(m_ProgramName, name.data())).first;
		}

		return it->second;
	}
}
