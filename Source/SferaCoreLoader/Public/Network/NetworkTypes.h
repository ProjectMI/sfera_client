#pragma once
#include "Core/Types.h"

struct FEndpoint 
{ 
	std::string Host;
	uint16 Port = 0; 
};

enum class EConnectionState 
{ 
	Closed, 
	Resolving, 
	Connecting, 
	Connected, 
	Failed 
};