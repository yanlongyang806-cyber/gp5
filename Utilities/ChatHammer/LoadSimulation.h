#ifndef LOADSIMULATION_H
#define LOADSIMULATION_H

void ReadLoadSimulationStruct();
void InitLoadSimulation(NetLink *link);
void UpdateLoadSimulator(NetLink *link);

bool IsLoadSimulationDone();

#endif //LOADSIMULATION_H