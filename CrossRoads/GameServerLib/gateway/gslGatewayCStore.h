/***************************************************************************
 
 
 
 *
 ***************************************************************************/

void SubscribeCStore(GatewaySession *psess, ContainerTracker *ptracker, void *pvParams);
bool IsModifiedCStore(GatewaySession *psess, ContainerTracker *ptracker);
bool CheckModifiedCStore(GatewaySession *psess, ContainerTracker *ptracker);
bool IsReadyCStore(GatewaySession *psess, ContainerTracker *ptracker);
void *CreateMappedCStore(GatewaySession *psess, ContainerTracker *ptracker, void *pvObj);
void DestroyMappedCStore(GatewaySession *psess, ContainerTracker *ptracker, void *pvObj);


// End of File
