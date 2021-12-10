#include "StringEqual.hpp"


namespace leopph::impl
{
	bool StringEqual::operator()(const std::string& left, const std::string& right) const
	{
		return left == right;
	}

	bool StringEqual::operator()(const std::string& left, std::string_view right) const
	{
		return left == right;
	}

	bool StringEqual::operator()(std::string_view left, const std::string& right) const
	{
		return left == right;
	}

	bool StringEqual::operator()(std::string_view left, std::string_view right) const
	{
		return left == right;
	}
}