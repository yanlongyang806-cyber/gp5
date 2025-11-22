#pragma once
GCC_SYSTEM

#ifndef ENTITYDATAQUERY_H_
#define ENTITYDATAQUERY_H_

#include "GlobalTypeEnum.h"

typedef struct ContainerRefArray	ContainerRefArray;

ContainerRefArray *GetAllContainersOfType(int type);

#endif
