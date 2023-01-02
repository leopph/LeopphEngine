#include "TransformComponent.hpp"

#include "Serialization.hpp"

#include <imgui.h>

#include <format>
#include <iostream>


namespace leopph {
	auto TransformComponent::UpdateWorldDataRecursive() -> void {
		mWorldPosition = mParent != nullptr ? mParent->mWorldRotation.Rotate(mParent->mWorldPosition + mLocalPosition) : mLocalPosition;
		mWorldRotation = mParent != nullptr ? mParent->mWorldRotation * mLocalRotation : mLocalRotation;
		mWorldScale = mParent != nullptr ? mParent->mWorldScale * mLocalScale : mLocalScale;

		mForward = mWorldRotation.Rotate(Vector3::forward());
		mRight = mWorldRotation.Rotate(Vector3::right());
		mUp = mWorldRotation.Rotate(Vector3::up());

		mModelMat[0] = Vector4{ mRight * mWorldScale, 0 };
		mModelMat[1] = Vector4{ mUp * mWorldScale, 0 };
		mModelMat[2] = Vector4{ mForward * mWorldScale, 0 };
		mModelMat[3] = Vector4{ mWorldPosition, 1 };

		mNormalMat[0] = mRight / mWorldScale;
		mNormalMat[1] = mUp / mWorldScale;
		mNormalMat[2] = mForward / mWorldScale;

		for (auto* const child : mChildren) {
			child->UpdateWorldDataRecursive();
		}
	}

	auto TransformComponent::GetWorldPosition() const -> Vector3 const& {
		return mWorldPosition;
	}


	auto TransformComponent::SetWorldPosition(Vector3 const& newPos) -> void {
		if (mParent != nullptr) {
			SetLocalPosition(mParent->mWorldRotation.conjugate().Rotate(newPos) - mParent->mWorldPosition);
		}
		else {
			SetLocalPosition(newPos);
		}
	}


	auto TransformComponent::GetLocalPosition() const -> Vector3 const& {
		return mLocalPosition;
	}


	auto TransformComponent::SetLocalPosition(Vector3 const& newPos) -> void {
		mLocalPosition = newPos;
		UpdateWorldDataRecursive();
	}


	auto TransformComponent::GetWorldRotation() const -> Quaternion const& {
		return mWorldRotation;
	}


	auto TransformComponent::SetWorldRotation(Quaternion const& newRot) -> void {
		if (mParent != nullptr) {
			SetLocalRotation(mParent->mWorldRotation.conjugate() * newRot);
		}
		else {
			SetLocalRotation(newRot);
		}
	}


	auto TransformComponent::GetLocalRotation() const -> Quaternion const& {
		return mLocalRotation;
	}


	auto TransformComponent::SetLocalRotation(Quaternion const& newRot) -> void {
		mLocalRotation = newRot;
		UpdateWorldDataRecursive();
	}


	auto TransformComponent::GetWorldScale() const -> Vector3 const& {
		return mWorldScale;
	}


	auto TransformComponent::SetWorldScale(Vector3 const& newScale) -> void {
		if (mParent != nullptr) {
			SetLocalScale(newScale / mParent->mWorldScale);
		}
		else {
			SetLocalScale(newScale);
		}
	}


	auto TransformComponent::GetLocalScale() const -> Vector3 const& {
		return mLocalScale;
	}


	auto TransformComponent::SetLocalScale(Vector3 const& newScale) -> void {
		mLocalScale = newScale;
		UpdateWorldDataRecursive();
	}


	auto TransformComponent::Translate(Vector3 const& vector, Space const base) -> void {
		if (base == Space::World) {
			SetWorldPosition(mWorldPosition + vector);
		}
		else if (base == Space::Local) {
			SetLocalPosition(mLocalPosition + mLocalRotation.Rotate(vector));
		}
	}


	auto TransformComponent::Translate(f32 const x, f32 const y, f32 const z, Space const base) -> void {
		Translate(Vector3{ x, y, z }, base);
	}


	auto TransformComponent::Rotate(Quaternion const& rotation, Space const base) -> void {
		if (base == Space::World) {
			SetLocalRotation(rotation * mLocalRotation);
		}
		else if (base == Space::Local) {
			SetLocalRotation(mLocalRotation * rotation);
		}
	}


	auto TransformComponent::Rotate(Vector3 const& axis, f32 const amountDegrees, Space const base) -> void {
		Rotate(Quaternion{ axis, amountDegrees }, base);
	}


	auto TransformComponent::Rescale(Vector3 const& scaling, Space const base) -> void {
		if (base == Space::World) {
			SetWorldScale(mWorldScale * scaling);
		}
		else if (base == Space::Local) {
			SetLocalScale(mLocalScale * scaling);
		}
	}


	auto TransformComponent::Rescale(f32 const x, f32 const y, f32 const z, Space const base) -> void {
		Rescale(Vector3{ x, y, z }, base);
	}


	auto TransformComponent::GetRightAxis() const -> Vector3 const& {
		return mRight;
	}


	auto TransformComponent::GetUpAxis() const -> Vector3 const& {
		return mUp;
	}


	auto TransformComponent::GetForwardAxis() const -> Vector3 const& {
		return mForward;
	}


	auto TransformComponent::GetParent() const -> TransformComponent* {
		return mParent;
	}

	auto TransformComponent::SetParent(TransformComponent* const parent) -> void {
		if (mParent) {
			std::erase(mParent->mChildren, this);
		}

		mParent = parent;

		if (mParent) {
			mParent->mChildren.push_back(this);
		}

		UpdateWorldDataRecursive();
	}


	auto TransformComponent::GetChildren() const -> std::span<TransformComponent* const> {
		return mChildren;
	}

	auto TransformComponent::GetModelMatrix() const -> Matrix4 const& {
		return mModelMat;
	}

	auto TransformComponent::GetNormalMatrix() const -> Matrix3 const& {
		return mNormalMat;
	}

	auto TransformComponent::CreateManagedObject() -> void {
		return ManagedAccessObject::CreateManagedObject("leopph", "Transform");
	}

	Object::Type const TransformComponent::SerializationType{ Type::Transform };

	auto TransformComponent::GetSerializationType() const -> Type {
		return Type::Transform;
	}

	auto TransformComponent::OnGui() -> void {
		if (ImGui::BeginTable(std::format("{}", GetGuid().ToString()).c_str(), 2, ImGuiTableFlags_SizingStretchSame)) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::PushItemWidth(FLT_MIN);
			ImGui::TableSetColumnIndex(1);
			ImGui::PushItemWidth(-FLT_MIN);

			ImGui::TableSetColumnIndex(0);
			ImGui::Text("Local Position");
			ImGui::TableNextColumn();

			Vector3 localPos{ mLocalPosition };
			if (ImGui::DragFloat3("###transformPos", localPos.get_data(), 0.1f)) {
				SetLocalPosition(localPos);
			}

			ImGui::TableNextColumn();
			ImGui::Text("Local Rotation");
			ImGui::TableNextColumn();

			auto euler{ mLocalRotation.ToEulerAngles() };
			if (ImGui::DragFloat3("###transformRot", euler.get_data(), 1.0f)) {
				SetLocalRotation(Quaternion::FromEulerAngles(euler));
			}

			ImGui::TableNextColumn();
			ImGui::Text("Local Scale");
			ImGui::TableNextColumn();

			Vector3 localScale{ mLocalScale };
			if (ImGui::DragFloat3("###transformScale", localScale.get_data(), 0.1f)) {
				SetLocalScale(localScale);
			}

			ImGui::EndTable();
		}
	}

	auto TransformComponent::SerializeTextual(YAML::Node& node) const -> void {
		Component::SerializeTextual(node);
		node["position"] = GetLocalPosition();
		node["rotation"] = GetLocalRotation();
		node["scale"] = GetLocalScale();
		if (GetParent()) {
			node["parent"] = GetParent()->GetGuid().ToString();
		}
		for (auto const* const child : mChildren) {
			node["children"].push_back(child->GetGuid().ToString());
		}
	}


	auto TransformComponent::DeserializeTextual(YAML::Node const& node) -> void {
		SetLocalPosition(node["position"].as<Vector3>(GetLocalPosition()));
		SetLocalRotation(node["rotation"].as<Quaternion>(GetLocalRotation()));
		SetLocalScale(node["scale"].as<Vector3>(GetLocalScale()));
		if (node["parent"]) {
			if (!node["parent"].IsScalar()) {
				auto const guidStr{ node["parent"].as<std::string>() };
				auto const parent{ dynamic_cast<TransformComponent*>(Object::FindObjectByGuid(Guid::Parse(guidStr))) };
				if (!parent) {
					std::cerr << "Failed to deserialize parent of Transform " << GetGuid().ToString() << ". Guid " << guidStr << " does not belong to any Transform." << std::endl;
				}
				SetParent(parent);
			}
		}
		if (node["children"]) {
			if (!node["children"].IsSequence()) {
				std::cerr << "Failed to deserialize children of Transform " << GetGuid().ToString() << ". Invalid data." << std::endl;
			}
			else {
				for (auto it{ node["children"].begin() }; it != node["children"].end(); ++it) {
					if (!it->IsScalar()) {
						std::cerr << "Failed to deserialize a child of Transform " << GetGuid().ToString() << ". Invalid data." << std::endl;
					}
					else {
						auto const guidStr{ it->as<std::string>() };
						auto const child{ dynamic_cast<TransformComponent*>(Object::FindObjectByGuid(Guid::Parse(guidStr))) };
						if (!child) {
							std::cerr << "Failed to deserialize a child of Transform " << GetGuid().ToString() << ". Guid " << guidStr << " does not belong to any Transform." << std::endl;
						}
						else {
							child->SetParent(this);
						}
					}
				}
			}
		}
	}

	namespace managedbindings {
		auto GetTransformWorldPosition(MonoObject* const transform) -> Vector3 {
			return ManagedAccessObject::GetNativePtrFromManagedObjectAs<TransformComponent*>(transform)->GetWorldPosition();
		}


		auto SetTransformWorldPosition(MonoObject* transform, Vector3 newPos) -> void {
			ManagedAccessObject::GetNativePtrFromManagedObjectAs<TransformComponent*>(transform)->SetWorldPosition(newPos);
		}


		auto GetTransformLocalPosition(MonoObject* transform) -> Vector3 {
			return ManagedAccessObject::GetNativePtrFromManagedObjectAs<TransformComponent*>(transform)->GetLocalPosition();
		}


		auto SetTransformLocalPosition(MonoObject* transform, Vector3 newPos) -> void {
			ManagedAccessObject::GetNativePtrFromManagedObjectAs<TransformComponent*>(transform)->SetLocalPosition(newPos);
		}


		auto GetTransformWorldRotation(MonoObject* transform) -> Quaternion {
			return ManagedAccessObject::GetNativePtrFromManagedObjectAs<TransformComponent*>(transform)->GetWorldRotation();
		}


		auto SetTransformWorldRotation(MonoObject* transform, Quaternion newRot) -> void {
			ManagedAccessObject::GetNativePtrFromManagedObjectAs<TransformComponent*>(transform)->SetWorldRotation(newRot);
		}


		auto GetTransformLocalRotation(MonoObject* transform) -> Quaternion {
			return ManagedAccessObject::GetNativePtrFromManagedObjectAs<TransformComponent*>(transform)->GetLocalRotation();
		}


		auto SetTransformLocalRotation(MonoObject* transform, Quaternion newRot) -> void {
			ManagedAccessObject::GetNativePtrFromManagedObjectAs<TransformComponent*>(transform)->SetLocalRotation(newRot);
		}


		auto GetTransformWorldScale(MonoObject* transform) -> Vector3 {
			return ManagedAccessObject::GetNativePtrFromManagedObjectAs<TransformComponent*>(transform)->GetWorldScale();
		}


		auto SetTransformWorldScale(MonoObject* transform, Vector3 newScale) -> void {
			ManagedAccessObject::GetNativePtrFromManagedObjectAs<TransformComponent*>(transform)->SetWorldScale(newScale);
		}


		auto GetTransformLocalScale(MonoObject* transform) -> Vector3 {
			return ManagedAccessObject::GetNativePtrFromManagedObjectAs<TransformComponent*>(transform)->GetLocalScale();
		}


		auto SetTransformLocalScale(MonoObject* transform, Vector3 newScale) -> void {
			ManagedAccessObject::GetNativePtrFromManagedObjectAs<TransformComponent*>(transform)->SetLocalScale(newScale);
		}


		auto TranslateTransformVector(MonoObject* transform, Vector3 vector, Space base) -> void {
			ManagedAccessObject::GetNativePtrFromManagedObjectAs<TransformComponent*>(transform)->Translate(vector, base);
		}


		auto TranslateTransform(MonoObject* transform, f32 x, f32 y, f32 z, Space base) -> void {
			ManagedAccessObject::GetNativePtrFromManagedObjectAs<TransformComponent*>(transform)->Translate(x, y, z, base);
		}


		auto RotateTransform(MonoObject* transform, Quaternion rotation, Space base) -> void {
			ManagedAccessObject::GetNativePtrFromManagedObjectAs<TransformComponent*>(transform)->Rotate(rotation, base);
		}


		auto RotateTransformAngleAxis(MonoObject* transform, Vector3 axis, f32 angleDegrees, Space base) -> void {
			ManagedAccessObject::GetNativePtrFromManagedObjectAs<TransformComponent*>(transform)->Rotate(axis, angleDegrees, base);
		}


		auto RescaleTransformVector(MonoObject* transform, Vector3 scaling, Space base) -> void {
			ManagedAccessObject::GetNativePtrFromManagedObjectAs<TransformComponent*>(transform)->Rescale(scaling, base);
		}


		auto RescaleTransform(MonoObject* transform, f32 x, f32 y, f32 z, Space base) -> void {
			ManagedAccessObject::GetNativePtrFromManagedObjectAs<TransformComponent*>(transform)->Rescale(x, y, z, base);
		}


		auto GetTransformRightAxis(MonoObject* transform) -> Vector3 {
			return ManagedAccessObject::GetNativePtrFromManagedObjectAs<TransformComponent*>(transform)->GetRightAxis();
		}


		auto GetTransformUpAxis(MonoObject* transform) -> Vector3 {
			return ManagedAccessObject::GetNativePtrFromManagedObjectAs<TransformComponent*>(transform)->GetUpAxis();
		}


		auto GetTransformForwardAxis(MonoObject* transform) -> Vector3 {
			return ManagedAccessObject::GetNativePtrFromManagedObjectAs<TransformComponent*>(transform)->GetForwardAxis();
		}


		auto GetTransformParent(MonoObject* transform) -> MonoObject* {
			auto const parent = ManagedAccessObject::GetNativePtrFromManagedObjectAs<TransformComponent*>(transform)->GetParent();
			return parent ? parent->GetManagedObject() : nullptr;
		}


		auto SetTransformParent(MonoObject* transform, MonoObject* parent) -> void {
			auto const nativeTransform = ManagedAccessObject::GetNativePtrFromManagedObjectAs<TransformComponent*>(transform);
			auto const nativeParent = parent ? static_cast<TransformComponent*>(ManagedAccessObject::GetNativePtrFromManagedObject(parent)) : nullptr;
			nativeTransform->SetParent(nativeParent);
		}


		auto GetTransformModelMatrix(MonoObject* transform) -> Matrix4 {
			return ManagedAccessObject::GetNativePtrFromManagedObjectAs<TransformComponent*>(transform)->GetModelMatrix();
		}


		auto GetTransformNormalMatrix(MonoObject* transform) -> Matrix3 {
			return ManagedAccessObject::GetNativePtrFromManagedObjectAs<TransformComponent*>(transform)->GetNormalMatrix();
		}
	}
}
