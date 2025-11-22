#pragma once

#define WI_FIND_REVISION_DIR "/findRevision/"

typedef struct WICWebRequest WICWebRequest;

bool wiHandleRevisions(SA_PARAM_NN_VALID WICWebRequest *pWebRequest);

typedef struct RevisionInfo RevisionInfo;
typedef struct rfSearchResponse rfSearchResponse;
