#pragma once

AUTO_STRUCT;
typedef struct DAnimTransform
{
	Vec3 vAxis;
	F32 fAngle;
	Vec3 vTranslation;
	Vec3 vScale;
} DAnimTransform;

AUTO_STRUCT;
typedef struct DAnimBone
{
	const char* pcName; AST(STRUCTPARAM POOL_STRING)
	int iFirstFrame;
	int iVersion;
	DAnimTransform** eaTransform;
} DAnimBone;

AUTO_STRUCT;
typedef struct DAnim
{
	DAnimBone** eaBone;
}DAnim;


// I moved these here so I could use them for anims too

typedef enum Reasons {
	REASON_PROCESSED,
	REASON_NOTYOURS,
	REASON_NOTNEWER,
	REASON_CHECKOUTFAILED,
	REASON_INVALID_SRC_DIR,
	REASON_BAD_DATA,
	REASON_COMPRESSION_FAILURE,
	REASON_INVALID_VERSION,
	REASON_EXCLUDED,
} Reasons;

Reasons processAnim(const char* fname, bool is_core, bool is_reprocess);
Reasons processSkeleton(const char* input_file_name, bool is_core, bool is_reprocess);

