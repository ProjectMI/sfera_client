#include "GameObjects/GameObjectTypes.h"
#include "Core/NumericParse.h"

namespace Sfera {
FObjectParamValue FObjectParamValue::Missing() { return {}; }
FObjectParamValue FObjectParamValue::Int(int32 value) { FObjectParamValue v; v.Type = EObjectParamType::Integer; v.IntValue = value; v.FloatValue = static_cast<float>(value); v.StringValue = std::to_string(value); return v; }
FObjectParamValue FObjectParamValue::Float(float value) { FObjectParamValue v; v.Type = EObjectParamType::Float; v.FloatValue = value; v.IntValue = static_cast<int32>(value); v.StringValue = std::to_string(value); return v; }
FObjectParamValue FObjectParamValue::String(std::string value) { FObjectParamValue v; v.Type = EObjectParamType::String; v.StringValue = std::move(value); int32 intValue = 0; float floatValue = 0.0f; if (NumericParse::TryParseInt32Strict(v.StringValue, intValue)) { v.IntValue = intValue; } if (NumericParse::TryParseFloatStrict(v.StringValue, floatValue)) { v.FloatValue = floatValue; } return v; }
}
