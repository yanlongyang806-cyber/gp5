#include "procAnim.h"
#include "utils.h"
#include "file.h"
#include "tree.h"
#include "Error.h"
#include "vrml.h"
#include "dynAnimTrack.h"
#include "Earray.h"
#include "quat.h"
#include "timing.h"
#include "FolderCache.h"
#include "gimmeDLLWrapper.h"
#include "windefinclude.h"
#include "fileutil.h"
#include "fileutil2.h"
#include "StringCache.h"
#include "mathutil.h"
#include "Wavelet.h"
#include "zutils.h"
#include "qsortG.h"

#include "main.h"

#include "dynSkeleton.h"
#include "dynNode.h"
#include "net/net.h"

#include "AutoGen/procAnim_h_ast.h"


const int iMaxExporterVersion = 150;
const int iATRKVersion = 200;

typedef enum eTrackType
{
	eRotation,
	ePosition,
	eScale
} eTrackType;

static void fixTranslation(const Vec3 vIn, Vec3 vOut);
static F32 getAllowableError(const char* pcBoneName, eTrackType eType);

const char* pcAnimDir = "animation_library/";
const char* pcAnimSkeletonDir = "animation_library/skeletons/";

char* getSkelNameFromSrcName(const char* src_name)
{
	const char* s;
	char* d;
	char pcResultBuffer[128];
	s = strstriConst(src_name, pcAnimDir) + strlen(pcAnimDir);
	strcpy_s(SAFESTR(pcResultBuffer), s);
	d = strstri(pcResultBuffer, ".wrl");
	if (d)
		*d = 0;
	return strdup(pcResultBuffer);
}

static bool animSrcToData(const char *src_name,char *data_name, size_t data_name_size, int is_core)
{
	const char	*s;
	char* d;

	s = strstriConst(src_name,pcAnimDir);
	if (!s)
	{
		FatalErrorf("Failed to find directory %s in path %s", pcAnimDir, src_name);
		return false;
	}
	s += strlen(pcAnimDir);
	if (is_core)
	{
		const char *core_data_dir = fileCoreDataDir();
		assert(core_data_dir);
		sprintf_s(SAFESTR2(data_name), "%s/%s/%s",core_data_dir,pcAnimDir,s);
	}
	else
	{
		sprintf_s(SAFESTR2(data_name), "%s/%s/%s",fileDataDir(),pcAnimDir,s);
	}

	// Also fix suffix
	d = strstri(data_name, ".danim");
	strcpy_s(d, data_name + data_name_size - d, ".atrk");
	forwardSlashes(data_name);
	return true;
}

static bool boneIsOK(char* pcBoneName)
{
	return !!strnicmp(pcBoneName, "GEO_", 4);
}


DAnimBone* dAnimBoneFindFromName(DAnim* pDAnim, const char* pcName)
{
	const U32 uiNumBones = eaSize(&pDAnim->eaBone);
	U32 uiBone;
	for (uiBone=0; uiBone<uiNumBones; ++uiBone)
	{
		DAnimBone* pBone = pDAnim->eaBone[uiBone];
		if (stricmp(pBone->pcName, pcName)==0)
			return pBone;
	}
	return NULL;
}

#define EPSILON 0.00001f

bool posKeyIsSame(const Vec3 vKey1, const Vec3 vKey2)
{
	Vec3 vDiff;
	subVec3(vKey1, vKey2, vDiff);
	return (lengthVec3Squared(vDiff) < EPSILON*EPSILON);
}

bool scaleKeyIsSame(const Vec3 vKey1, const Vec3 vKey2)
{
	Vec3 vDiff;
	subVec3(vKey1, vKey2, vDiff);
	return (lengthVec3Squared(vDiff) < EPSILON*EPSILON);
}

// This function works by forcing the quaterions to the same side of a 4-sphere and taking their linear distance
// which is an approximate measure of distance for small angles of difference (which is what we are concerned with)
bool rotKeyIsSame(Quat qKey1, const Quat qKey2_const)
{
	Vec4 vDiff;
	Quat qKey2;
	copyQuat(qKey2_const, qKey2);
	quatForceWPositive(qKey1);
	quatForceWPositive(qKey2);
	subVec4(qKey1, qKey2, vDiff);
	return (lengthVec4Squared(vDiff) < EPSILON*EPSILON);
}


static void setSkelToFrame(DynNode* pBone, const DynAnimTrack* pAnimTrack, U32 uiFrame, const Vec3 vParentPos, const DynNode* pBaseBone)
{
	DynNode* pChild = pBone->pChild;
	const DynBoneTrack* pBoneTrack;
	Vec3 vNewParentPos;

	if (stashFindPointerConst(pAnimTrack->boneTable, pBone->pcTag, &pBoneTrack))
	{
		Vec3 vBonePos;
		const DynNode* pBaseBoneMatch = dynNodeFindByNameConst(pBaseBone, pBone->pcTag, false);

		subVec3(pBoneTrack->posKeys[0].vPos, vParentPos, vNewParentPos);
		addVec3(vNewParentPos,pBoneTrack->posKeys[uiFrame].vPos, vBonePos);
		dynNodeSetPos(pBone, vBonePos);
		dynNodeSetRot(pBone, pBoneTrack->rotKeys[uiFrame].qRot);

		if ( pBaseBoneMatch )
		{
			Vec3 vDiff;
			Vec3 vBaseBonePos;
			dynNodeGetLocalPos(pBaseBoneMatch, vBaseBonePos);
			subVec3(vNewParentPos, vBaseBonePos, vDiff);
			if ( lengthVec3(vDiff) > 0.00001f )
			{
				int i = 0;
			}
		}
	}
	while (pChild)
	{
		setSkelToFrame(pChild, pAnimTrack, uiFrame, pBoneTrack->posKeys[0].vPos, pBaseBone);
		pChild = pChild->pSibling;
	}
}

static bool verifyWorldSpaceTransforms(DynNode* pBone, const DynAnimTrack* pBackupAnimTrack, U32 uiFrame)
{
	DynNode* pChild = pBone->pChild;
	const DynBoneTrack* pBoneTrack;
	bool bReturn = true;

	if (stashFindPointerConst(pBackupAnimTrack->boneTable, pBone->pcTag, &pBoneTrack))
	{
		Vec3 vPosDiff;
		Vec3 vBonePos;
		dynNodeGetWorldSpacePos(pBone, vBonePos);
		subVec3(pBoneTrack->posKeys[uiFrame].vPos, vBonePos, vPosDiff);
		if (lengthVec3(vPosDiff) > 0.000001f)
		{
			//printf("Error: Bone %s in frame %d is off by %f!\n", pBone->pcTag, uiFrame, lengthVec3(vPosDiff));
			bReturn = false;
		}
		if (stricmp(pBone->pcTag, "FootL")==0)
		{
			printf("Foot Height on frame %d = %f\n", uiFrame, vBonePos[1]);
		}
	}
	while (pChild)
	{
		bReturn = verifyWorldSpaceTransforms(pChild, pBackupAnimTrack, uiFrame) && bReturn;
		pChild = pChild->pSibling;
	}
	return bReturn;
}

static void verifyAccuracy(DynAnimTrack* pAnimTrack, DynAnimTrack* pBackupAnimTrack, const DynNode* pBase)
{
	// We want to make sure that the relative transforms in the anim track concatenate to equal the backup animations
	// which are the original, absolute space anims

	// First, set up initial poses
	DynNode* pBone = dynNodeAllocTreeCopy(pBase, NULL, true, NULL);
	U32 uiFrame;
	for (uiFrame=1; uiFrame < pAnimTrack->uiTotalFrames; ++uiFrame)
	{
		setSkelToFrame(pBone, pAnimTrack, uiFrame, zerovec3, pBase);

		verifyWorldSpaceTransforms(pBone, pBackupAnimTrack, uiFrame);
	}

	dynNodeFreeTree(pBone);
}

const U32 uiFirstRotSubTrackIndex = 0;
const U32 uiFirstPosSubTrackIndex = 3;
const U32 uiFirstScaleSubTrackIndex = 6;
const F32 fOneOverFFFE = 0.000015259254; // 1.0 / 0xFFFE (65534)


void dynBoneTrackUncompressedFree(DynBoneTrackUncompressed* pTrack)
{
	SAFE_FREE(pTrack->pqRot);
}

/*
static bool verifyCompressionAccuracy(DynAnimTrack* pAnimTrack)
{
	U32 uiBone;
	U32 uiTotalBones = 0;
	for (uiBone=0; uiBone<pAnimTrack->uiBoneCount; ++uiBone)
	{
		DynBoneTrack* pBoneTrack = &pAnimTrack->bones[uiBone];
		DynBoneTrackCompressed* pBoneTrackCompressed = &pAnimTrack->bonesCompressed[uiBone];
		DynBoneTrackUncompressed uncompressedBone = {0};
		U32 uiKey;
		F32 fMaxDiff = 0.0f;
		F32 fAllowableError, fAllowableAngleError;
		int i;
		dynBoneTrackDecompress(pBoneTrackCompressed, &uncompressedBone, pAnimTrack->uiTotalFrames);

		// Check rotation
		fAllowableError = getAllowableError(pBoneTrack->pcBoneName, eRotation);
		{
			Vec3 vPYR;
			Quat qError;
			setVec3same(vPYR, fAllowableError);
			PYRToQuat(vPYR, qError);
			quatToAxisAngle(qError, vPYR, &fAllowableAngleError);
			fAllowableAngleError = fabsf(fAllowableAngleError);
			if (fAllowableAngleError > PI)
				fAllowableAngleError = fabsf(fAllowableAngleError - TWOPI);
		}
		for (uiKey=1; uiKey<pBoneTrack->uiRotKeyCount; ++uiKey)
		{
			Quat qOrig, qNew, qInvNew, qDiff;
			Vec3 vAxis;
			F32 fAngleError;
			copyQuat(pBoneTrack->rotKeys[uiKey].qRot, qOrig);
			if (uncompressedBone.pqRot)
				copyQuat(uncompressedBone.pqRot[pBoneTrack->rotKeyFrames[uiKey].uiFrame], qNew);
			else
				copyQuat(uncompressedBone.qStaticRot, qNew);
			quatForceWPositive(qOrig);
			quatForceWPositive(qNew);
			quatInverse(qNew, qInvNew);
			quatMultiply(qOrig, qInvNew, qDiff);
			quatToAxisAngle(qDiff, vAxis, &fAngleError);
			fAngleError = fabsf(fAngleError);
			if (fAngleError > PI)
				fAngleError = fabsf(fAngleError - TWOPI);
			fMaxDiff = MAX(fMaxDiff, fAngleError);
			if (fAngleError > fAllowableAngleError)
			{
				int g = 0;
			}
		}
		if (fMaxDiff > fAllowableAngleError)
		{
			Errorf("Got a max angle difference of %.2f degrees, which is too much!", DEG(fMaxDiff));
			return false;
		}

		// Check position
		fAllowableError = getAllowableError(pBoneTrack->pcBoneName, ePosition);
		fMaxDiff = 0.0f;
		for (uiKey=1; uiKey<pBoneTrack->uiPosKeyCount; ++uiKey)
		{
			Vec3 vOrig, vNew;
			copyVec3(pBoneTrack->posKeys[uiKey].vPos, vOrig);
			if (uncompressedBone.pvPos)
				copyVec3(uncompressedBone.pvPos[pBoneTrack->posKeyFrames[uiKey].uiFrame], vNew);
			else
				copyVec3(uncompressedBone.vStaticPos, vNew);

			for (i=0; i<3; ++i)
			{
				F32 fDiff = fabsf(vNew[i] - vOrig[i]);
				fMaxDiff = MAX(fMaxDiff, fDiff);
				if (fDiff > fAllowableError)
				{
					int g = 0;
				}
			}
		}
		if (fMaxDiff > fAllowableError)
		{
			Errorf("Got a max position difference of %.2f feet, which is too much!", fMaxDiff);
			return false;
		}

		// Check scale
		fAllowableError = getAllowableError(pBoneTrack->pcBoneName, eScale);
		fMaxDiff = 0.0f;
		for (uiKey=1; uiKey<pBoneTrack->uiScaleKeyCount; ++uiKey)
		{
			Vec3 vOrig, vNew;
			copyVec3(pBoneTrack->scaleKeys[uiKey].vScale, vOrig);
			if (uncompressedBone.pvScale)
				copyVec3(uncompressedBone.pvScale[pBoneTrack->scaleKeyFrames[uiKey].uiFrame], vNew);
			else
				copyVec3(uncompressedBone.vStaticScale, vNew);

			for (i=0; i<3; ++i)
			{
				F32 fDiff = fabsf(vNew[i] - vOrig[i]);
				fMaxDiff = MAX(fMaxDiff, fDiff);
				if (fDiff > fAllowableError)
				{
					int g = 0;
				}
			}
		}
		if (fMaxDiff > fAllowableError)
		{
			Errorf("Got a max scale difference of %.2f, which is too much!", fMaxDiff);
			return false;
		}
	}
	return true;
}
*/

int coeffSort(const F32 *a, const F32 *b)
{
	F32 fA = fabsf(*a);
	F32 fB = fabsf(*b);
	if (fA == fB)
		return 0;
	else if (fB > fA)
		return 1;
	return -1;
}

// Finds the threshold necessary to keep the top fKeepFraction of all coeffs
F32 findWaveletCoefThreshold(const F32* pfCoeffs, U32 uiNumCoeffs, U32 uiNumToKeep)
{
	//Find fraction to Keep
	F32* pfSorted = _alloca(sizeof(F32) * uiNumCoeffs);
	memcpy(pfSorted, pfCoeffs, sizeof(F32) * uiNumCoeffs);
	qsortG(pfSorted, uiNumCoeffs, sizeof(F32), coeffSort);
	return fabsf(pfSorted[uiNumToKeep-1]);
}

bool attemptWaveletReconstruction( const F32* pfCoefs, U16* puiCoeffs, U32 uiNumKeysPow2, const F32* pfOriginalValues, U32 uiNumKeys, U32 uiNumToKeep, DynBoneWaveletTrack* pWaveletTrack, F32 fAllowableError) 
{
	F32 fThreshold = findWaveletCoefThreshold(pfCoefs, uiNumKeysPow2, uiNumToKeep);
	U32 uiFrame;
	F32 fMinCoef = FLT_MAX;
	F32 fMaxCoef = -FLT_MAX;

	// Quantize and zip them there wavelet coefficients


	// First, calculate min, max, and thus range of all thresholded values
	for (uiFrame=0; uiFrame<uiNumKeysPow2; ++uiFrame)
	{
		if (fabsf(pfCoefs[uiFrame]) >= fThreshold)
		{
			fMinCoef = MIN(fMinCoef, pfCoefs[uiFrame]);
			fMaxCoef = MAX(fMaxCoef, pfCoefs[uiFrame]);
		}
	}
	pWaveletTrack->fRange = fMaxCoef - fMinCoef;
	pWaveletTrack->fMinCoef = fMinCoef;

	// Now normalize and quantize all coeffs, setting sub-threshold values to 0
	for (uiFrame=0; uiFrame<uiNumKeysPow2; ++uiFrame)
	{
		if (fabsf(pfCoefs[uiFrame]) < fThreshold)
			puiCoeffs[uiFrame] = 0;
		else
		{
			F32 fNormalized = (pfCoefs[uiFrame] - fMinCoef) / pWaveletTrack->fRange;
			puiCoeffs[uiFrame] = (U16)round(fNormalized * 0xFFFE);
			assert(puiCoeffs[uiFrame] != 0xFFFF);
			++puiCoeffs[uiFrame];
		}
	}
	pWaveletTrack->fRange /= (F32)0xFFFE; // we store that aspect of the scaling in the fRange so it will be only one multiply at decompression time

	// Now that we have a decent packed set, see if it satisfies the allowable error


	{
		F32* pfNewCoeffs = _alloca(sizeof(F32) * uiNumKeysPow2);
		F32* pfNewValues = _alloca(sizeof(F32) * uiNumKeysPow2);
		// Decompress
		for (uiFrame=0; uiFrame<uiNumKeysPow2; ++uiFrame)
		{
			if (puiCoeffs[uiFrame] == 0)
				pfNewCoeffs[uiFrame] = 0.0f;
			else
				pfNewCoeffs[uiFrame] = ((F32)(puiCoeffs[uiFrame]-1) * pWaveletTrack->fRange) + pWaveletTrack->fMinCoef;
		}

		inverseWaveletTransform(pfNewCoeffs, uiNumKeysPow2, pfNewValues);

		// Check for error
		for (uiFrame=0; uiFrame<uiNumKeys; ++uiFrame)
		{
			F32 fError = fabsf(pfNewValues[uiFrame] - pfOriginalValues[uiFrame]);
			if (fError > fAllowableError)
				return false;
		}
	}
	// Survived error test!
	return true;
}


static void compressToWaveletTrack( DynBoneWaveletTrack* pWaveletTrack, F32* pfInputKeys, U32 uiNumKeys, F32 fAllowableError, const char* pcDebugTrackName)
{
	U32 uiNumKeysPow2 = pow2(uiNumKeys) * 2;
	F32* pfInput = _alloca(sizeof(F32) * uiNumKeysPow2);
	F32* pfCoefs = _alloca(sizeof(F32) * uiNumKeysPow2);
	F32* pfOutput = _alloca(sizeof(F32) * uiNumKeysPow2);
	F32* pfDiff = _alloca(sizeof(F32) * uiNumKeysPow2);
	U32 uiFrame;
	U16* puiCoeffs = _alloca(sizeof(U16) * uiNumKeysPow2);
	U32 uiNumToKeep = 2;

	/*
	F32 fMean = 0.0f;

	for (uiFrame=0; uiFrame<uiNumKeys; ++uiFrame)
		fMean += pfInputKeys[uiFrame];
	fMean /= (F32)(uiNumKeys);
	*/


	for (uiFrame=0; uiFrame<uiNumKeysPow2; ++uiFrame)
	{
		if (uiFrame < uiNumKeys)
			pfInput[uiFrame] = pfInputKeys[uiFrame];
		else
			pfInput[uiFrame] = 0.0f;
	}

	fastWaveletTransform(pfInput, uiNumKeysPow2, pfCoefs);

	while (!attemptWaveletReconstruction(pfCoefs, puiCoeffs, uiNumKeysPow2, pfInput, uiNumKeys, uiNumToKeep, pWaveletTrack, fAllowableError))
	{
		if (uiNumToKeep >= uiNumKeysPow2)
		{
			Errorf("Unable to compress animation %s within the error tolerance.", pcDebugTrackName);
			break;
		}
		++uiNumToKeep;
	}

	// We've found an acceptable coeff set, so zip it
	pWaveletTrack->zippedCoefs = zipData(puiCoeffs, sizeof(U16)*uiNumKeysPow2, &pWaveletTrack->uiZipLength);
}

static bool isSubTrackIdentity(F32* pfFrames, U32 uiNumFrames, F32 fIdentityValue, F32 fThreshold)
{
	U32 uiFrame;
	for (uiFrame=0; uiFrame<uiNumFrames; ++uiFrame)
	{
		if (fabsf(pfFrames[uiFrame] - fIdentityValue) > fThreshold)
			return false;
	}
	return true;
}

static bool isSubTrackStatic(F32* pfFrames, U32 uiNumFrames, SA_PARAM_NN_VALID F32* pfAverage, F32 fThreshold)
{
	U32 uiFrame;
	F32 fMin = pfFrames[0];
	F32 fMax = pfFrames[0];
	F32 fAverage = pfFrames[0];
	for (uiFrame=1; uiFrame<uiNumFrames; ++uiFrame)
	{
		fMin = MIN(fMin, pfFrames[uiFrame]);
		fMax = MAX(fMax, pfFrames[uiFrame]);
		fAverage += pfFrames[uiFrame];
	}
	fAverage /= (F32)uiNumFrames;
	*pfAverage = fAverage;
	return ( 
		(fMax - fAverage) < fThreshold
		&& (fAverage - fMin) < fThreshold
		);
}

#define MATCHES_BONE(a) (stricmp(pcBoneName, a)==0)

static F32 getAllowableError(const char* pcBoneName, eTrackType eType)
{
	// Legs and hips
	if (true) // MATCHES_BONE("ULegL") || MATCHES_BONE("LLegL") || MATCHES_BONE("ULegR") || MATCHES_BONE("LLegR") || MATCHES_BONE("Hips"))
	{
		switch (eType)
		{
			xcase eRotation:
				return RAD(0.25f);
			xcase ePosition:
				return 0.005f;
			xcase eScale:
			default:
				return 0.01f;
		}
	}
	else if (MATCHES_BONE("Chest") || MATCHES_BONE("UArmR") || MATCHES_BONE("UArmL") || MATCHES_BONE("LArmR") || MATCHES_BONE("LArmL") || MATCHES_BONE("Head")) // upper body
	{
		switch (eType)
		{
			xcase eRotation:
				return RAD(0.5f);
			xcase ePosition:
				return 0.05f;
			xcase eScale:
			default:
				return 0.05f;
		}
	}

	// Default is low quality
	switch (eType)
	{
		xcase eRotation:
			return RAD(2.0f);
		xcase ePosition:
			return 0.1f;
		xcase eScale:
		default:
			return 0.1f;
	}
}

// Assumes q is normalized!!!
static void quatToPYRDecomposition(const Quat q, Vec3 pyr)
{
	F32 test = quatX(q)*quatY(q) + quatZ(q)*quatW(q);
	if (test > 0.49999f) { // singularity at north pole
		pyr[2] = HALFPI;
		pyr[1] = -2.0f * atan2f(quatX(q),quatW(q));
		pyr[0] = 0;
		return;
	}
	if (test < -0.49999f) { // singularity at south pole
		pyr[1] = 2.0f * atan2f(quatX(q),quatW(q));
		pyr[2] = -HALFPI;
		pyr[0] = 0;
		return;
	}
	{
		F32 sqx = SQR(quatX(q));
		F32 sqy = SQR(quatY(q));
		F32 sqz = SQR(quatZ(q));
		pyr[2] = asinf(2.0f*test);
		pyr[1] = -atan2f(2.0f*quatY(q)*quatW(q)-2.0f*quatX(q)*quatZ(q) , 1.0f - 2.0f*sqy - 2.0f*sqz);
		pyr[0] = atan2f(2.0f*quatX(q)*quatW(q)-2.0f*quatY(q)*quatZ(q) , 1.0f - 2.0f*sqx - 2.0f*sqz);
	}
}

static U32 convertAnimTrackBonesToEulersAndCompress(DynAnimTrack* pAnimTrack)
{
	U32 uiBone;
	U32 uiTotalBones = 0;
	pAnimTrack->bonesCompressed = calloc(sizeof(DynBoneTrackCompressed), pAnimTrack->uiBoneCount);
	for (uiBone=0; uiBone<pAnimTrack->uiBoneCount; ++uiBone)
	{
		DynBoneTrack* pBoneTrack = &pAnimTrack->bones[uiBone];
		DynBoneTrackCompressed* pBoneTrackCompressed = &pAnimTrack->bonesCompressed[uiBone];
		pBoneTrackCompressed->pcBoneName = pBoneTrack->pcBoneName;
		pBoneTrackCompressed->pWaveletTracks = calloc(sizeof(DynBoneWaveletTrack), 9); // allocate the maximum possible number of tracks for now
		pBoneTrackCompressed->pStaticTracks = calloc(sizeof(DynBoneStaticTrack), 9); // allocate the maximum possible number of tracks for now

		
		// Now, process each track, one at a time, first with rotation
		{
			F32* eaRotKeys[3] = {0};
			U32 uiKeyIndex;
			int i;
			F32 fAllowableError = getAllowableError(pBoneTrack->pcBoneName, eRotation);
			for (uiKeyIndex=1; uiKeyIndex<pBoneTrack->uiRotKeyCount; ++uiKeyIndex)
			{
				Vec3 vPYR;
				quatToPYRDecomposition(pBoneTrack->rotKeys[uiKeyIndex].qRot, vPYR);
				for (i=0; i<3; ++i)
					eafPush(&eaRotKeys[i], vPYR[i]);
			}
			for (i=0; i<3; ++i)
			{
				U32 uiNumKeys = eafSize(&eaRotKeys[i]);
				if (uiNumKeys > 0 && !isSubTrackIdentity(eaRotKeys[i], uiNumKeys, 0.0f, fAllowableError)) // if the track is basically a long list of the identity value, don't even send it, that's default
				{
					// If the track is pretty much static, just write a static track (which is just the average value)
					if (isSubTrackStatic(eaRotKeys[i], uiNumKeys, &pBoneTrackCompressed->pStaticTracks[uiFirstRotSubTrackIndex+i].fValue, fAllowableError))
						pBoneTrackCompressed->uiStaticTracks |= (1<<(uiFirstRotSubTrackIndex + i));
					else // otherwise, wavelet compression time!
					{
						DynBoneWaveletTrack* pWaveletTrack = &pBoneTrackCompressed->pWaveletTracks[uiFirstRotSubTrackIndex + i];
						compressToWaveletTrack(pWaveletTrack, eaRotKeys[i], uiNumKeys, fAllowableError, pAnimTrack->pcName);
						pBoneTrackCompressed->uiWaveletTracks |= (1<<(uiFirstRotSubTrackIndex + i));
					}
				}
			}
			for (i=0; i<3; ++i)
				eafDestroy(&eaRotKeys[i]);
		}

		// Next do position
		{
			F32* eaPosKeys[3] = {0};
			U32 uiKeyIndex;
			int i;
			F32 fAllowableError = getAllowableError(pBoneTrack->pcBoneName, ePosition);
			for (uiKeyIndex=1; uiKeyIndex<pBoneTrack->uiPosKeyCount; ++uiKeyIndex)
			{
				for (i=0; i<3; ++i)
					eafPush(&eaPosKeys[i], pBoneTrack->posKeys[uiKeyIndex].vPos[i]);
			}
			for (i=0; i<3; ++i)
			{
				U32 uiNumKeys = eafSize(&eaPosKeys[i]);
				if (uiNumKeys > 0 && !isSubTrackIdentity(eaPosKeys[i], uiNumKeys, 0.0f, fAllowableError)) // if the track is basically a long list of the identity value, don't even send it, that's default
				{
					// If the track is pretty much static, just write a static track (which is just the average value)
					if (isSubTrackStatic(eaPosKeys[i], uiNumKeys, &pBoneTrackCompressed->pStaticTracks[uiFirstPosSubTrackIndex+i].fValue, fAllowableError))
						pBoneTrackCompressed->uiStaticTracks |= (1<<(uiFirstPosSubTrackIndex + i));
					else // otherwise, wavelet compression time!
					{
						DynBoneWaveletTrack* pWaveletTrack = &pBoneTrackCompressed->pWaveletTracks[uiFirstPosSubTrackIndex + i];
						compressToWaveletTrack(pWaveletTrack, eaPosKeys[i], uiNumKeys, fAllowableError, pAnimTrack->pcName);
						pBoneTrackCompressed->uiWaveletTracks |= (1<<(uiFirstPosSubTrackIndex + i));
					}
				}
				eafDestroy(&eaPosKeys[i]);
			}
		}

		// Finally, scale
		{
			F32* eaScaleKeys[3] = {0};
			U32 uiKeyIndex;
			int i;
			F32 fAllowableError = getAllowableError(pBoneTrack->pcBoneName, eScale);
			for (uiKeyIndex=1; uiKeyIndex<pBoneTrack->uiScaleKeyCount; ++uiKeyIndex)
			{
				for (i=0; i<3; ++i)
					eafPush(&eaScaleKeys[i], pBoneTrack->scaleKeys[uiKeyIndex].vScale[i]);
			}
			for (i=0; i<3; ++i)
			{
				U32 uiNumKeys = eafSize(&eaScaleKeys[i]);
				if (uiNumKeys > 0 && !isSubTrackIdentity(eaScaleKeys[i], uiNumKeys, 1.0f, fAllowableError)) // if the track is basically a long list of the identity value, don't even send it, that's default
				{
					// If the track is pretty much static, just write a static track (which is just the average value)
					if (isSubTrackStatic(eaScaleKeys[i], uiNumKeys, &pBoneTrackCompressed->pStaticTracks[uiFirstScaleSubTrackIndex+i].fValue, fAllowableError))
						pBoneTrackCompressed->uiStaticTracks |= (1<<(uiFirstScaleSubTrackIndex + i));
					else // otherwise, wavelet compression time!
					{
						DynBoneWaveletTrack* pWaveletTrack = &pBoneTrackCompressed->pWaveletTracks[uiFirstScaleSubTrackIndex + i];
						compressToWaveletTrack(pWaveletTrack, eaScaleKeys[i], uiNumKeys, fAllowableError, pAnimTrack->pcName);
						pBoneTrackCompressed->uiWaveletTracks |= (1<<(uiFirstScaleSubTrackIndex + i));
					}
				}
				eafDestroy(&eaScaleKeys[i]);
			}
		}

		// now repackage tracks using the correct numbers
		{
			DynBoneWaveletTrack* pTempWavelet = pBoneTrackCompressed->pWaveletTracks;
			DynBoneStaticTrack* pTempStatic = pBoneTrackCompressed->pStaticTracks;
			U32 uiWaveletTracks = pBoneTrackCompressed->uiWaveletTracks;
			U32 uiStaticTracks = pBoneTrackCompressed->uiStaticTracks;
			U32 uiIndex;
			pBoneTrackCompressed->pWaveletTracks = calloc(sizeof(DynBoneWaveletTrack), countBitsSparse(uiWaveletTracks));
			pBoneTrackCompressed->pStaticTracks = calloc(sizeof(DynBoneStaticTrack), countBitsSparse(uiStaticTracks));

			uiIndex=0;
			while (uiWaveletTracks)
			{
				U32 uiLowBitIndex = lowBitIndex(uiWaveletTracks);
				memcpy(&pBoneTrackCompressed->pWaveletTracks[uiIndex++], &pTempWavelet[uiLowBitIndex], sizeof(DynBoneWaveletTrack));
				CLRB(&uiWaveletTracks, uiLowBitIndex);
			}

			uiIndex = 0;
			while (uiStaticTracks)
			{
				U32 uiLowBitIndex = lowBitIndex(uiStaticTracks);
				memcpy(&pBoneTrackCompressed->pStaticTracks[uiIndex++], &pTempStatic[uiLowBitIndex], sizeof(DynBoneStaticTrack));
				CLRB(&uiStaticTracks, uiLowBitIndex);
			}

#pragma warning(suppress:6001) // /analyze "Using uninitialized memory '*pTempStatic'"
			free(pTempStatic);
#pragma warning(suppress:6001) // /analyze "Using uninitialized memory '*pTempWavelet'"
			free(pTempWavelet);
		}

		if (pBoneTrackCompressed->uiWaveletTracks || pBoneTrackCompressed->uiStaticTracks)
			++uiTotalBones;
	}
	return uiTotalBones;
}


static U32 compressAnimTrack(DynAnimTrack* pAnimTrack)
{
	U32 uiBone;
	U32 uiTotalBones = 0;
	for (uiBone=0; uiBone<pAnimTrack->uiBoneCount; ++uiBone)
	{
		DynBoneTrack* pBoneTrack = &pAnimTrack->bones[uiBone];

		DynPosKey** eaPosKeys = NULL;
		DynPosKeyFrame** eaPosKeyFrames = NULL;

		DynRotKey** eaRotKeys = NULL;
		DynRotKeyFrame** eaRotKeyFrames = NULL;

		DynScaleKey** eaScaleKeys = NULL;
		DynScaleKeyFrame** eaScaleKeyFrames = NULL;

		DynPosKey* pOldPosKeys = pBoneTrack->posKeys;
		DynPosKeyFrame* pOldPosKeyFrames = pBoneTrack->posKeyFrames;

		DynRotKey* pOldRotKeys = pBoneTrack->rotKeys;
		DynRotKeyFrame* pOldRotKeyFrames = pBoneTrack->rotKeyFrames;

		DynScaleKey* pOldScaleKeys = pBoneTrack->scaleKeys;
		DynScaleKeyFrame* pOldScaleKeyFrames = pBoneTrack->scaleKeyFrames;

		// First, look to see if any keys are non-identity
		{
			U32 uiFrame;
			Vec3 vPreviousPosKey;
			Quat qPreviousRotKey;
			Vec3 vPreviousScaleKey;
			bool bLastFrameAdded;
			zeroVec3(vPreviousPosKey);
			unitQuat(qPreviousRotKey);
			unitVec3(vPreviousScaleKey);

			bLastFrameAdded = true;
			for (uiFrame=1; uiFrame<pBoneTrack->uiPosKeyCount; ++uiFrame)
			{
				// Always include first and last frames
				if (uiFrame==1 || !posKeyIsSame(vPreviousPosKey, pBoneTrack->posKeys[uiFrame].vPos))
				{
					// We need to include the last frame after skipping some, so we interp properly
					if (!bLastFrameAdded)
					{
						eaPush(&eaPosKeys, &pBoneTrack->posKeys[uiFrame-1]);
						eaPush(&eaPosKeyFrames, &pBoneTrack->posKeyFrames[uiFrame-1]);
					}
					eaPush(&eaPosKeys, &pBoneTrack->posKeys[uiFrame]);
					eaPush(&eaPosKeyFrames, &pBoneTrack->posKeyFrames[uiFrame]);
					copyVec3(pBoneTrack->posKeys[uiFrame].vPos, vPreviousPosKey);
					bLastFrameAdded = true;
				}
				else
					bLastFrameAdded = false;

			}

			bLastFrameAdded = true;
			for (uiFrame=1; uiFrame<pBoneTrack->uiRotKeyCount; ++uiFrame)
			{
				// Always include first and last frames
				if (uiFrame==1 || !rotKeyIsSame(qPreviousRotKey, pBoneTrack->rotKeys[uiFrame].qRot))
				{
					// Just in case there was some error introduced:
					//quatNormalize(pBoneTrack->rotKeys[uiFrame].qRot);
					if (!bLastFrameAdded)
					{
						eaPush(&eaRotKeys, &pBoneTrack->rotKeys[uiFrame-1]);
						eaPush(&eaRotKeyFrames, &pBoneTrack->rotKeyFrames[uiFrame-1]);
					}
					eaPush(&eaRotKeys, &pBoneTrack->rotKeys[uiFrame]);
					eaPush(&eaRotKeyFrames, &pBoneTrack->rotKeyFrames[uiFrame]);
					copyQuat(pBoneTrack->rotKeys[uiFrame].qRot, qPreviousRotKey);
					bLastFrameAdded = true;
				}
				else
					bLastFrameAdded = false;
			}

			bLastFrameAdded = true;
			for (uiFrame=1; uiFrame<pBoneTrack->uiScaleKeyCount; ++uiFrame)
			{
				// Always include first and last frames
				if (uiFrame==1 || !scaleKeyIsSame(vPreviousScaleKey, pBoneTrack->scaleKeys[uiFrame].vScale))
				{
					if (!bLastFrameAdded)
					{
						eaPush(&eaScaleKeys, &pBoneTrack->scaleKeys[uiFrame-1]);
						eaPush(&eaScaleKeyFrames, &pBoneTrack->scaleKeyFrames[uiFrame-1]);
					}
					eaPush(&eaScaleKeys, &pBoneTrack->scaleKeys[uiFrame]);
					eaPush(&eaScaleKeyFrames, &pBoneTrack->scaleKeyFrames[uiFrame]);
					copyVec3(pBoneTrack->scaleKeys[uiFrame].vScale, vPreviousScaleKey);
					bLastFrameAdded = true;
				}
				else
					bLastFrameAdded = false;
			}
		}

		// Copy over the keys we decided to keep
		pBoneTrack->uiPosKeyCount = eaSize(&eaPosKeys);
		pBoneTrack->uiRotKeyCount = eaSize(&eaRotKeys);
		pBoneTrack->uiScaleKeyCount = eaSize(&eaScaleKeys);

		if (pBoneTrack->uiPosKeyCount > 1
			|| ( pBoneTrack->uiPosKeyCount == 1 && !posKeyIsSame(eaPosKeys[0]->vPos, zerovec3) )
			)
		{
			U32 uiKey;
			pBoneTrack->posKeys = calloc(sizeof(DynPosKey), pBoneTrack->uiPosKeyCount);
			pBoneTrack->posKeyFrames = calloc(sizeof(DynPosKeyFrame), pBoneTrack->uiPosKeyCount);
			for (uiKey=0; uiKey<pBoneTrack->uiPosKeyCount; ++uiKey)
			{
				memcpy(&pBoneTrack->posKeys[uiKey], eaPosKeys[uiKey], sizeof(DynPosKey));
				memcpy(&pBoneTrack->posKeyFrames[uiKey], eaPosKeyFrames[uiKey], sizeof(DynPosKeyFrame));
			}
		}
		else
		{
			pBoneTrack->posKeys = NULL;
			pBoneTrack->posKeyFrames = NULL;
			pBoneTrack->uiPosKeyCount = 0;
		}

		if (pBoneTrack->uiRotKeyCount > 1
			|| ( pBoneTrack->uiRotKeyCount == 1 && !rotKeyIsSame(eaRotKeys[0]->qRot, unitquat) )
			)
		{
			U32 uiKey;
			pBoneTrack->rotKeys = calloc(sizeof(DynRotKey), pBoneTrack->uiRotKeyCount);
			pBoneTrack->rotKeyFrames = calloc(sizeof(DynRotKeyFrame), pBoneTrack->uiRotKeyCount);
			for (uiKey=0; uiKey<pBoneTrack->uiRotKeyCount; ++uiKey)
			{
				memcpy(&pBoneTrack->rotKeys[uiKey], eaRotKeys[uiKey], sizeof(DynRotKey));
				memcpy(&pBoneTrack->rotKeyFrames[uiKey], eaRotKeyFrames[uiKey], sizeof(DynRotKeyFrame));
			}
		}
		else
		{
			pBoneTrack->rotKeys = NULL;
			pBoneTrack->rotKeyFrames = NULL;
			pBoneTrack->uiRotKeyCount = 0;
		}

		if (pBoneTrack->uiScaleKeyCount > 1
			|| ( pBoneTrack->uiRotKeyCount == 1 && !scaleKeyIsSame(eaScaleKeys[0]->vScale, onevec3) )
			)
		{
			U32 uiKey;
			pBoneTrack->scaleKeys = calloc(sizeof(DynScaleKey), pBoneTrack->uiScaleKeyCount);
			pBoneTrack->scaleKeyFrames = calloc(sizeof(DynScaleKeyFrame), pBoneTrack->uiScaleKeyCount);
			for (uiKey=0; uiKey<pBoneTrack->uiScaleKeyCount; ++uiKey)
			{
				memcpy(&pBoneTrack->scaleKeys[uiKey], eaScaleKeys[uiKey], sizeof(DynScaleKey));
				memcpy(&pBoneTrack->scaleKeyFrames[uiKey], eaScaleKeyFrames[uiKey], sizeof(DynScaleKeyFrame));
			}
		}
		else
		{
			pBoneTrack->scaleKeys = NULL;
			pBoneTrack->scaleKeyFrames = NULL;
			pBoneTrack->uiScaleKeyCount = 0;
		}

		/*
		pBoneTrack->uiPosKeyCount = pBoneTrack->uiRotKeyCount = pBoneTrack->uiPosKeyCount-1;
		pBoneTrack->posKeys = calloc(sizeof(DynPosKey), pBoneTrack->uiPosKeyCount);
		pBoneTrack->rotKeys = calloc(sizeof(DynRotKey), pBoneTrack->uiRotKeyCount);
		memcpy(pBoneTrack->posKeys, &pOldPosKeys[1], pBoneTrack->uiPosKeyCount * sizeof(DynPosKey));
		memcpy(pBoneTrack->rotKeys, &pOldRotKeys[1], pBoneTrack->uiRotKeyCount * sizeof(DynRotKey));
		*/

		eaDestroy(&eaPosKeys);
		eaDestroy(&eaPosKeyFrames);
		eaDestroy(&eaRotKeys);
		eaDestroy(&eaRotKeyFrames);
		eaDestroy(&eaScaleKeys);
		eaDestroy(&eaScaleKeyFrames);

#pragma warning(suppress:6001) // /analyze "Using uninitialized memory '*pOldPosKeys'"
		free(pOldPosKeys);
#pragma warning(suppress:6001) // /analyze "Using uninitialized memory '*pOldPosKeyFrames'"
		free(pOldPosKeyFrames);
#pragma warning(suppress:6001) // /analyze "Using uninitialized memory '*pOldRotKeys'"
		free(pOldRotKeys);
#pragma warning(suppress:6001) // /analyze "Using uninitialized memory '*pOldRotKeyFrames'"
		free(pOldRotKeyFrames);
#pragma warning(suppress:6001) // /analyze "Using uninitialized memory '*pOldScaleKeys'"
		free(pOldScaleKeys);
#pragma warning(suppress:6001) // /analyze "Using uninitialized memory '*pOldScaleKeyFrames'"
		free(pOldScaleKeyFrames);

		if (pBoneTrack->uiPosKeyCount || pBoneTrack->uiScaleKeyCount || pBoneTrack->uiRotKeyCount)
			++uiTotalBones;

	}
	return uiTotalBones;
}

void danimCreateBoneList( DynNode* pBone, DAnim* pDAnim, DAnimBone*** peaBoneList ) 
{
	while (pBone)
	{
		DAnimBone* pAnimBone = dAnimBoneFindFromName(pDAnim, pBone->pcTag);
		if (pAnimBone)
			eaPush(peaBoneList, pAnimBone);
		if (pBone->pChild)
			danimCreateBoneList(pBone->pChild, pDAnim, peaBoneList);
		pBone = pBone->pSibling;
	}
}

static void fixTranslation(const Vec3 vIn, Vec3 vOut)
{
	F32 fSwap;
	vOut[0] = -vIn[0];
	fSwap = vIn[2];
	vOut[2] = -vIn[1];
	vOut[1] = fSwap;
}

static void fixUpAnimTrackTransforms(DynAnimTrack* pAnimTrack, DynNode* pBone, DynNode* pParent)
{
	// Process all children first! We want a bottom-up calculation.
	DynNode* pChild = pBone->pChild;
	DynBoneTrack* pBoneTrack;
	DynNode* pChildsParentNode = pBone; // default is the child's node is this one
	if (!stashFindPointer(pAnimTrack->boneTable, pBone->pcTag, &pBoneTrack))
	{
		// We can't find this bone in our animtrack list, 
		// so pretend like it's not in the skeleton when we process children
		pChildsParentNode = pParent;
		pBoneTrack = NULL;
	}
	while (pChild)
	{
		fixUpAnimTrackTransforms(pAnimTrack, pChild, pBone);
		pChild = pChild->pSibling;
	}

	// Now that all children have been processed, we can fix up our transforms according to our parent's transforms.
	// We make each of our transform frames relative to the parent's transforms. It's important to do this
	// bottom up, so that our absolute xform is relative to our parent's absolue xform, thus making
	// the transforms local
	if (pBoneTrack)
	{
		DynBoneTrack* pParentBoneTrack = NULL;
		U32 uiFrame;

		DynNode* pNextParent;
		Vec3 vParentOffset;
		Vec3 vParentLocal;

		setVec3(vParentOffset, 0.0f, 0.0f , 0.0f);
		
		// if we don't find the parent bone in the animation, we assume it is in it's default position
		if(pParent && !stashFindPointer(pAnimTrack->boneTable, pParent->pcTag, &pParentBoneTrack))
		{

			dynNodeGetLocalPos(pParent, vParentLocal);
			copyVec3(vParentLocal, vParentOffset);
			pNextParent = pParent->pParent;

			// see if we can find any valid parents, and track the total local offsets of the ones we can't find
			while(pNextParent && !stashFindPointer(pAnimTrack->boneTable, pNextParent->pcTag, &pParentBoneTrack))
			{
				dynNodeGetLocalPos(pNextParent, vParentLocal);
				addVec3(vParentOffset, vParentLocal, vParentOffset);
				pNextParent = pNextParent->pParent;
			}

		}

		// Only reprocess children if there is a parent bone track
		if (pParent && pParentBoneTrack)
		{
			// Now we can change the space, since they have no child left to process
			for (uiFrame=1; uiFrame<pBoneTrack->uiPosKeyCount; ++uiFrame)
			{
				// Calc rotation
				// qLocal * qParentAbs = qOurAbs
				// qLocal = inv(qParentAbs) * qOurAbs
				Quat qLocal, qInvParentAbs;
				quatInverse(pParentBoneTrack->rotKeys[uiFrame].qRot, qInvParentAbs);
				quatMultiply(pBoneTrack->rotKeys[uiFrame].qRot, qInvParentAbs, qLocal);
				// Normalize the result, and copy it to the rotation keys
				quatNormalize(qLocal);
				copyQuat(qLocal, pBoneTrack->rotKeys[uiFrame].qRot);


				// Calc translation
				{
					Vec3 vAbsPosDiff;
					Vec3 vParentSpacePosDiff;
					Vec3 vLocalBonePos;
					dynNodeGetLocalPos(pBone, vLocalBonePos);
					// Calculate how far, in abs. space, the bone pos is from the parent bone pos
					subVec3(pBoneTrack->posKeys[uiFrame].vPos, pParentBoneTrack->posKeys[uiFrame].vPos, vAbsPosDiff);


					// Rotate this abs diff into the space of the parent bone
					quatRotateVec3(qInvParentAbs, vAbsPosDiff, vParentSpacePosDiff);

					// Subtract the original bone offset to get the translation (this calc is in parent bone space)
					{
						int i;
						for (i=0; i<3; ++i)
							vParentSpacePosDiff[i] /= pParentBoneTrack->scaleKeys[uiFrame].vScale[i];
					}

					subVec3(vParentSpacePosDiff, vLocalBonePos, pBoneTrack->posKeys[uiFrame].vPos);

					// also subtract our phantom parent offset, rotation and scale don't need this because they are assumed to be identity
					subVec3(pBoneTrack->posKeys[uiFrame].vPos, vParentOffset, pBoneTrack->posKeys[uiFrame].vPos);
				}

				// Calc scale
				{
					int i;
					for (i=0; i<3; ++i)
						pBoneTrack->scaleKeys[uiFrame].vScale[i] /= pParentBoneTrack->scaleKeys[uiFrame].vScale[i];
				}
			}
		}
		else // we know it's a root now
		{
			for (uiFrame=1; uiFrame<pBoneTrack->uiPosKeyCount; ++uiFrame)
			{
				Vec3 vLocalBonePos;
				dynNodeGetLocalPos(pBone, vLocalBonePos);
				// Just leave the rotation, as it is already relative to identity quat

				// Calc translation, relative to original bone pos
				subVec3(pBoneTrack->posKeys[uiFrame].vPos, vLocalBonePos, pBoneTrack->posKeys[uiFrame].vPos);
			}
		}
	}
}

//#define VERIFY_BONETRACKS 1

static Reasons convertDanimToAnimTrack( char* pcDanimFileName, DynAnimTrack* pAnimTrack, const DynBaseSkeleton* pBaseSkel, int iVersion, U32* puiTotalBones)
{
	DAnim danim = {0};
	DAnimBone** eaBoneList = NULL;
#if VERIFY_BONETRACKS
	DynAnimTrack* pBackupAnimTrack;
#endif
	loadstart_printf("Reading and converting danim to atrk format..");

	// Read Vrml file into tree of root
	ParserLoadFiles(NULL, pcDanimFileName, 0, 0, parse_DAnim, &danim);


	// Create bone list
	danimCreateBoneList(pBaseSkel->pRoot, &danim, &eaBoneList);

	// Make backup anim track for verifying calcs later

	pAnimTrack->uiBoneCount = eaSize(&eaBoneList);
	if (!pAnimTrack->uiBoneCount)
	{
		Errorf("Failed to find any matching bone animation data in anim %s with skeleton %s", pcDanimFileName, pBaseSkel->pcName);
		loadend_printf(" Failed");
		return REASON_INVALID_SRC_DIR;
	}
	pAnimTrack->bones = calloc(sizeof(DynBoneTrack), pAnimTrack->uiBoneCount);
	pAnimTrack->uiTotalFrames = eaSize(&(eaBoneList[0]->eaTransform));
	pAnimTrack->boneTable = stashTableCreateWithStringKeys(pow2(pAnimTrack->uiBoneCount*2), StashDefault);
	if (g_no_compression)
		pAnimTrack->eType = eDynAnimTrackType_Uncompressed;
	else
		pAnimTrack->eType = eDynAnimTrackType_Compressed;
#if VERIFY_BONETRACKS
	pBackupAnimTrack = calloc(sizeof(DynAnimTrack), 1);
	pBackupAnimTrack->uiBoneCount = pAnimTrack->uiBoneCount;
	pBackupAnimTrack->bones = calloc(sizeof(DynBoneTrack), pAnimTrack->uiBoneCount);
	pBackupAnimTrack->uiTotalFrames = pAnimTrack->uiTotalFrames;
	pBackupAnimTrack->boneTable = stashTableCreateWithStringKeys(pow2(pBackupAnimTrack->uiBoneCount*2), StashDefault);
#endif
	if (pAnimTrack->uiTotalFrames < 2)
	{
		Errorf("Not enough frames %d in danim %s", pAnimTrack->uiTotalFrames, pcDanimFileName);
		loadend_printf(" Failed");
		return REASON_INVALID_SRC_DIR;
	}

	// Copy info over to pAnimTrack
	{
		U32 uiBoneIndex;
		for (uiBoneIndex=0; uiBoneIndex<pAnimTrack->uiBoneCount; ++uiBoneIndex)
		{
			DynBoneTrack* pBoneTrack = &pAnimTrack->bones[uiBoneIndex];
#if VERIFY_BONETRACKS
			DynBoneTrack* pBackupBoneTrack = &pBackupAnimTrack->bones[uiBoneIndex];
#endif
			DAnimBone* pAnimBone = eaBoneList[uiBoneIndex];

			if (pAnimBone->iVersion != iVersion)
			{
				/*
				Errorf("Animation version %d does not match GetVrml Animation processor version %d", pAnimBone->iVersion, iVersion);
				return REASON_INVALID_VERSION;
				*/
			}
			pBoneTrack->pcBoneName = allocAddString(pAnimBone->pcName);

			pBoneTrack->uiPosKeyCount = pBoneTrack->uiRotKeyCount = pBoneTrack->uiScaleKeyCount = pAnimTrack->uiTotalFrames; // for now
			pBoneTrack->posKeys = calloc(sizeof(DynPosKey), pBoneTrack->uiPosKeyCount);
			pBoneTrack->posKeyFrames = calloc(sizeof(DynPosKeyFrame), pBoneTrack->uiPosKeyCount);
			pBoneTrack->rotKeys = calloc(sizeof(DynRotKey), pBoneTrack->uiRotKeyCount);
			pBoneTrack->rotKeyFrames = calloc(sizeof(DynRotKeyFrame), pBoneTrack->uiRotKeyCount);
			pBoneTrack->scaleKeys = calloc(sizeof(DynScaleKey), pBoneTrack->uiScaleKeyCount);
			pBoneTrack->scaleKeyFrames = calloc(sizeof(DynScaleKeyFrame), pBoneTrack->uiScaleKeyCount);

#if VERIFY_BONETRACKS
			pBackupBoneTrack->pcBoneName = pBoneTrack->pcBoneName;
			pBackupBoneTrack->uiPosKeyCount = pBackupBoneTrack->uiRotKeyCount = pAnimTrack->uiTotalFrames; // for now
			pBackupBoneTrack->posKeys = calloc(sizeof(DynPosKey), pBackupBoneTrack->uiPosKeyCount);
			pBackupBoneTrack->rotKeys = calloc(sizeof(DynRotKey), pBackupBoneTrack->uiRotKeyCount);
#endif
			// Copy over absolue transforms to keys, then go through and fix them up
			{
				U32 uiKeyIndex;
				assert((U32)eaSize(&pAnimBone->eaTransform) == pAnimTrack->uiTotalFrames);
				for (uiKeyIndex=0; uiKeyIndex<pAnimTrack->uiTotalFrames; ++uiKeyIndex)
				{
					DAnimTransform* pTransForm = pAnimBone->eaTransform[uiKeyIndex];
					Vec3 vAxis;
					pBoneTrack->posKeyFrames[uiKeyIndex].uiFrame = uiKeyIndex?uiKeyIndex-1:0;
					pBoneTrack->rotKeyFrames[uiKeyIndex].uiFrame = uiKeyIndex?uiKeyIndex-1:0;
					pBoneTrack->scaleKeyFrames[uiKeyIndex].uiFrame = uiKeyIndex?uiKeyIndex-1:0;

					pBoneTrack->posKeys[uiKeyIndex].vPos[0] = -pTransForm->vTranslation[0];
					pBoneTrack->posKeys[uiKeyIndex].vPos[1] = pTransForm->vTranslation[2];
					pBoneTrack->posKeys[uiKeyIndex].vPos[2] = -pTransForm->vTranslation[1];

					pBoneTrack->scaleKeys[uiKeyIndex].vScale[0] = pTransForm->vScale[0];
					pBoneTrack->scaleKeys[uiKeyIndex].vScale[1] = pTransForm->vScale[2];
					pBoneTrack->scaleKeys[uiKeyIndex].vScale[2] = pTransForm->vScale[1];
					// Early (pre-versioned) versions of .danim files do not support scale, give it a neutral scale
					if (pAnimBone->iVersion == 0)
					{
						copyVec3(onevec3, pBoneTrack->scaleKeys[uiKeyIndex].vScale);
					}
					else
					{
						int i;
						for (i=0; i<3; ++i)
						{
							if (pBoneTrack->scaleKeys[uiKeyIndex].vScale[i] <= 0.0f)
							{
								Errorf("Scale must be greater than zero!");
								loadend_printf(" Failed");
								return REASON_BAD_DATA;
							}
						}
					}

					vAxis[0] = -pTransForm->vAxis[0];
					vAxis[1] = pTransForm->vAxis[2];
					vAxis[2] = -pTransForm->vAxis[1];

					axisAngleToQuat(vAxis, -pTransForm->fAngle, pBoneTrack->rotKeys[uiKeyIndex].qRot);
					quatNormalize(pBoneTrack->rotKeys[uiKeyIndex].qRot);

#if VERIFY_BONETRACKS
					pBackupBoneTrack->posKeys[uiKeyIndex].uiFrame = pBoneTrack->posKeys[uiKeyIndex].uiFrame;
					pBackupBoneTrack->rotKeys[uiKeyIndex].uiFrame = pBoneTrack->rotKeys[uiKeyIndex].uiFrame;
					copyQuat(pBoneTrack->rotKeys[uiKeyIndex].qRot, pBackupBoneTrack->rotKeys[uiKeyIndex].qRot);
					copyVec3(pBoneTrack->posKeys[uiKeyIndex].vPos, pBackupBoneTrack->posKeys[uiKeyIndex].vPos);
#endif
					/*
					if (uiKeyIndex==0)
						setVec4(pBoneTrack->rotKeys[uiKeyIndex].qRot, -0.70710677, 0, 0, -0.70710677);
						*/
				}
			}
			stashAddPointer(pAnimTrack->boneTable, pBoneTrack->pcBoneName, pBoneTrack, false);
#if VERIFY_BONETRACKS
			stashAddPointer(pBackupAnimTrack->boneTable, pBackupBoneTrack->pcBoneName, pBackupBoneTrack, false);
#endif
		}
	}

	// Fix up transforms
	fixUpAnimTrackTransforms(pAnimTrack, pBaseSkel->pRoot, NULL);

#if VERIFY_BONETRACKS
	verifyAccuracy(pAnimTrack, pBackupAnimTrack, pBaseSkel->pRoot);
#endif

	// Compress animtrack
	switch (pAnimTrack->eType)
	{
		xcase eDynAnimTrackType_Uncompressed:
		{
			U32 uiTotalBones = compressAnimTrack(pAnimTrack);
			if (puiTotalBones)
				*puiTotalBones = uiTotalBones;
		}
		xcase eDynAnimTrackType_Compressed:
		{
			U32 uiTotalBones;
			loadstart_printf("Optimizing animation compression...");
			uiTotalBones = convertAnimTrackBonesToEulersAndCompress(pAnimTrack);
			if (puiTotalBones)
				*puiTotalBones = uiTotalBones;
			loadend_printf("done");
			/*
			if (!verifyCompressionAccuracy(pAnimTrack))
				return REASON_COMPRESSION_FAILURE;
				*/
		}
	}


	// Listify Nodes
	/*
	listifyNodeTree(pRoot, &eaNodeList);
	pAnimTrack->uiBoneCount = uiNodeCount = eaSize(&eaNodeList);
	pAnimTrack->bones = malloc( sizeof(DynBoneTrack) * uiNodeCount );
	{
		U32 uiIndex;
		for (uiIndex=0; uiIndex < uiNodeCount; ++uiIndex)
		{
			Node* pNode = eaNodeList[uiIndex];
			convertNodeToBoneTrack(pNode, &pAnimTrack->bones[uiIndex]);
		}
	}
	*/

	loadend_printf(" Done");
	StructDeInit(parse_DAnim, &danim);
	return REASON_PROCESSED;
}

static U32 boneTrackSize( const DynBoneTrack* pBoneTrack )
{
	U32 uiSize = 0;
	uiSize += (sizeof(DynPosKey) + sizeof(DynPosKeyFrame)) * pBoneTrack->uiPosKeyCount;
	uiSize += (sizeof(DynRotKey) + sizeof(DynRotKeyFrame)) * pBoneTrack->uiRotKeyCount;
	uiSize += (sizeof(DynScaleKey) + sizeof(DynScaleKeyFrame)) * pBoneTrack->uiScaleKeyCount;

	return uiSize;
}

static void outputWaveletTrack( FILE* file, DynBoneWaveletTrack* pWavelet )
{
	fwrite(&pWavelet->fMinCoef, sizeof(F32), 1, file);
	fwrite(&pWavelet->fRange, sizeof(F32), 1, file);
	fwrite(&pWavelet->uiZipLength, sizeof(U32), 1, file);
	fwrite(pWavelet->zippedCoefs, sizeof(char), pWavelet->uiZipLength, file);
}

static void outputBoneTrackCompressed( FILE* file, DynBoneTrackCompressed* pBoneTrack )
{
	U32 uiNumStaticTracks, uiNumWaveletTracks, uiTrackIndex;
	// First write length of name, then string
	{
		U32 uiLen = strlen(pBoneTrack->pcBoneName);
		fwrite(&uiLen, sizeof(U32), 1, file);
		fwrite(pBoneTrack->pcBoneName, sizeof(char), uiLen+1, file);
	}
	// Write the flags indicating which (and how many) of both kinds of compressed tracks there are
	fwrite(&pBoneTrack->uiStaticTracks, sizeof(U16), 1, file);
	fwrite(&pBoneTrack->uiWaveletTracks, sizeof(U16), 1, file);

	uiNumStaticTracks = countBitsSparse(pBoneTrack->uiStaticTracks);
	uiNumWaveletTracks = countBitsSparse(pBoneTrack->uiWaveletTracks);

	// Write static tracks (just 1 float)
	for (uiTrackIndex=0; uiTrackIndex<uiNumStaticTracks; ++uiTrackIndex)
	{
		fwrite(&pBoneTrack->pStaticTracks[uiTrackIndex].fValue, sizeof(F32), 1, file);
	}

	// Now write wavelet tracks
	for (uiTrackIndex=0; uiTrackIndex<uiNumWaveletTracks; ++uiTrackIndex)
		outputWaveletTrack(file, &pBoneTrack->pWaveletTracks[uiTrackIndex]);
}

static void outputBoneTrack( FILE* file, DynBoneTrack* pBoneTrack )
{
	// First write length of name, then string
	{
		U32 uiLen = strlen(pBoneTrack->pcBoneName);
		fwrite(&uiLen, sizeof(U32), 1, file);
		fwrite(pBoneTrack->pcBoneName, sizeof(char), uiLen+1, file);
	}

	// Write the pos key count and the rot key count
	fwrite(&pBoneTrack->uiPosKeyCount, sizeof(U32), 1, file);
	fwrite(&pBoneTrack->uiRotKeyCount, sizeof(U32), 1, file);
	fwrite(&pBoneTrack->uiScaleKeyCount, sizeof(U32), 1, file);

	// Now write the pos keys
	{
		U32 uiPosKeyIndex;
		for (uiPosKeyIndex=0; uiPosKeyIndex<pBoneTrack->uiPosKeyCount; ++uiPosKeyIndex)
		{
			// write the key time
			fwrite(&pBoneTrack->posKeyFrames[uiPosKeyIndex].uiFrame, sizeof(U32), 1, file);
			// write the pos vec
			fwrite(&pBoneTrack->posKeys[uiPosKeyIndex].vPos, sizeof(Vec3), 1, file);
		}
	}

	// Now write the rotkeys
	{
		U32 uiRotKeyIndex;
		for (uiRotKeyIndex=0; uiRotKeyIndex<pBoneTrack->uiRotKeyCount; ++uiRotKeyIndex)
		{
			// write the key time
			fwrite(&pBoneTrack->rotKeyFrames[uiRotKeyIndex].uiFrame, sizeof(U32), 1, file);
			// write the rot quat
			fwrite(&pBoneTrack->rotKeys[uiRotKeyIndex].qRot, sizeof(Quat), 1, file);
		}
	}

	// Now write the scale keys
	{
		U32 uiScaleKeyIndex;
		for (uiScaleKeyIndex=0; uiScaleKeyIndex<pBoneTrack->uiScaleKeyCount; ++uiScaleKeyIndex)
		{
			// write the key time
			fwrite(&pBoneTrack->scaleKeyFrames[uiScaleKeyIndex].uiFrame, sizeof(U32), 1, file);
			// write the scale vec
			fwrite(&pBoneTrack->scaleKeys[uiScaleKeyIndex].vScale, sizeof(Vec3), 1, file);
		}
	}

//	return MAX(pBoneTrack->posKeys?pBoneTrack->posKeys[pBoneTrack->uiPosKeyCount-1].uiFrame:0, pBoneTrack->rotKeys?pBoneTrack->rotKeys[pBoneTrack->uiRotKeyCount-1].uiFrame:0);
}

static void outputTimeStamp(char* fname)
{
	char dstTimeStamp[MAX_PATH];
	FILE* file;
	changeFileExt(fname, ".timestamp", dstTimeStamp);

	loadstart_printf("Writing file %s..", dstTimeStamp);
	file = fopen( dstTimeStamp, "w" );
	if ( !file )
	{
		FatalErrorf("Failed to open file %s\n", dstTimeStamp);
		loadend_printf("Failed, returning.");
		return;
	}

	fprintf(file, "%d", timeSecondsSince2000());
	fclose(file);
	loadend_printf(" Done");
}

const int iPlaceHolder = -1;

static void outputAnimTrack( char* fname, DynAnimTrack* pAnimTrack, U32 uiTotalBones)
{
	FILE* file;
	loadstart_printf("Writing output file: %s..", fname);
	file = fopen( fname, "wb" );
	if ( !file )
	{
		FatalErrorf("Failed to open file %s\n", fname);
		loadend_printf("Failed, returning.");;
		return;
	}
	// First write a place holder meant to jump start a versioning system
	fwrite(&iPlaceHolder, sizeof(U32), 1, file);
	// Then write the version
	fwrite(&iATRKVersion, sizeof(int), 1, file);
	// Then write length of name, then string
	{
		U32 uiLen = strlen(pAnimTrack->pcName);
		fwrite(&uiLen, sizeof(U32), 1, file);
		fwrite(pAnimTrack->pcName, sizeof(char), uiLen+1, file);
	}

	fwrite(&pAnimTrack->eType, sizeof(eDynAnimTrackType), 1, file);

	
	// Then write bone count, and then loop through bones and write them
	//pAnimTrack->uiTotalFrames = 0; // uiTotalFrames is now calculated in the compressAnimTrack()
	fwrite(&uiTotalBones, sizeof(U32), 1, file);

	switch (pAnimTrack->eType)
	{
		xcase eDynAnimTrackType_Uncompressed:
		{
			U32 uiBoneTrackIndex;
			U32 uiBoneTrackCount = pAnimTrack->uiBoneCount;
			U32 uiBoneTracksWritten = 0;
			U32 uiBoneTrackSize = 0;
			pAnimTrack->uiBoneCount = uiTotalBones;
			for (uiBoneTrackIndex=0; uiBoneTrackIndex<uiBoneTrackCount; ++uiBoneTrackIndex)
			{
				uiBoneTrackSize += boneTrackSize(&pAnimTrack->bones[uiBoneTrackIndex]);
			}
			fwrite(&uiBoneTrackSize, sizeof(U32), 1, file);
			for (uiBoneTrackIndex=0; uiBoneTrackIndex<uiBoneTrackCount; ++uiBoneTrackIndex)
			{
				if (
					pAnimTrack->bones[uiBoneTrackIndex].uiPosKeyCount 
					|| pAnimTrack->bones[uiBoneTrackIndex].uiRotKeyCount 
					|| pAnimTrack->bones[uiBoneTrackIndex].uiScaleKeyCount 
					)
				{
					outputBoneTrack(file, &pAnimTrack->bones[uiBoneTrackIndex]);
					++uiBoneTracksWritten;
				}
				//pAnimTrack->uiTotalFrames = MAX(pAnimTrack->uiTotalFrames, uiFramesForTrack);
			}
			if ( uiBoneTracksWritten != pAnimTrack->uiBoneCount )
			{
				FatalErrorf("Somehow wrote %d tracks when there are %d in animation %s", uiBoneTracksWritten, pAnimTrack->uiBoneCount, pAnimTrack->pcName);
			}
		}
		xcase eDynAnimTrackType_Compressed:
		{
			U32 uiBoneTrackIndex;
			U32 uiBoneTrackCount = pAnimTrack->uiBoneCount;
			U32 uiBoneTracksWritten = 0;
			pAnimTrack->uiBoneCount = uiTotalBones;
			for (uiBoneTrackIndex=0; uiBoneTrackIndex<uiBoneTrackCount; ++uiBoneTrackIndex)
			{
				DynBoneTrackCompressed* pBoneTrack = &pAnimTrack->bonesCompressed[uiBoneTrackIndex];
				if 	( pBoneTrack->uiStaticTracks || pBoneTrack->uiWaveletTracks)
				{
					outputBoneTrackCompressed(file, pBoneTrack);
					++uiBoneTracksWritten;
				}
			}
			if ( uiBoneTracksWritten != pAnimTrack->uiBoneCount )
			{
				FatalErrorf("Somehow wrote %d tracks when there are %d in animation %s", uiBoneTracksWritten, pAnimTrack->uiBoneCount, pAnimTrack->pcName);
			}
		}

	}
	
	// Finally, Write the max frames count. Note we subtract one, since we throw away the 0 frame
	--pAnimTrack->uiTotalFrames;
	assert(pAnimTrack->uiTotalFrames < 1024 && pAnimTrack->uiTotalFrames > 0);
	fwrite(&pAnimTrack->uiTotalFrames, sizeof(U32), 1, file);


	// done with the file
	fclose(file);
	loadend_printf(" Done");
}

// If this returns RESAON_PROCESSED, we're still inside a loadstart_printf, caller must call loadend_printf
Reasons checkIfFileShouldBeProcessed(const char* srcFile, const char* dstFile, const char* pcAttachedSkelFileName, bool reprocessing_on_the_fly )
{
	char dstTimeStamp[MAX_PATH];
	changeFileExt(dstFile, ".timestamp", dstTimeStamp);
	if  (g_force_rebuild || !fileExists(dstTimeStamp) || fileNewerAbsolute(dstTimeStamp,srcFile) || (pcAttachedSkelFileName && fileNewerAbsolute(dstTimeStamp, pcAttachedSkelFileName)))
	{
		int ret;
		char srcFileCopy[MAX_PATH];

		/* rules for processing:
		When GetVrml sees a .WRL file that is newer than its corresponding .geo file, it sees that this
		file needs to be processed.  Previously it would then just check out the .geo file and process it.
		Now, it will NOT process the file if the .WRL is checked out by someone else.  If no one has the
		file checked out, then it will only process it if you were the last person to check it in.  There
		should no longer be any issues with people getting their .geo files checked out by someone else.

		The correct procedure to things is :
		1. Make your changes (Check out .WRL file)
		2. Process the geometry (run GetVRML)
		3. TEST (Run the game)
		4. Check-in (or check-point) your files so other people can get them
		*/

		// vrml_name == .wrl file
		// out_fname == output .geo file

		// Don't do this anymore, since we're only processing our own files, we don't care
		//if (fileNewer(out_fname, vrml_name)) // Get the latest version just to make sure
		//	ret=gimmeDLLDoOperation(out_fname, GIMME_GLV, GIMME_QUIET);



		const char *lastauthor = gimmeDLLQueryLastAuthor(srcFile);

		printf("\n");
		if (!pcAttachedSkelFileName)
			loadstart_printf("Processing Skeleton %s\n", srcFile);
		else
			loadstart_printf("Processing Anim %s\n", srcFile);

		// We're going to cancel if the WRL locked and it's locked by someone other than me
		//   or it's not locked and I'm not the last author of the WRL
		if (!g_force_rebuild && !gimmeDLLQueryIsFileMine(srcFile))
		{
			if (1) { // !g_output_dir[0]) { // Don't warn if we're outputting to a special (sparse) folder
				consoleSetFGColor(COLOR_RED|COLOR_BRIGHT);
				printf("    Warning: .atrk file is older than .WRL file, but you do not have the .WRL file checked\n");
				printf("      out, so it is being skipped.  ");
				if (stricmp(lastauthor, "You do not have the latest version")==0) {
					printf("You do not have the latest version of the .WRL file.\n");
				} else {
					printf("%s may have forgot to process it before checking it in.\n", lastauthor);
				}
				consoleSetFGColor(COLOR_RED|COLOR_GREEN|COLOR_BLUE);
			}
			loadend_printf("Done.");
			return REASON_NOTYOURS;
		}

		//			if (!fileExists(out_fname)) {
		//				FILE *fnew;
		strcpy(srcFileCopy, srcFile);
		mkdirtree(srcFileCopy);
		//				fnew = fopen(out_fname, "w");
		//				fclose(fnew);
		//			}

		if (!g_no_checkout) {
			if (!gimmeDLLQueryIsFileLockedByMeOrNew(dstFile))
				ret=gimmeDLLDoOperation(dstFile, GIMME_CHECKOUT, GIMME_QUIET);
			else
				ret = NO_ERROR;
			// Also grab the time stamp file
			if (ret == NO_ERROR && !gimmeDLLQueryIsFileLockedByMeOrNew(dstTimeStamp))
				ret=gimmeDLLDoOperation(dstTimeStamp, GIMME_CHECKOUT, GIMME_QUIET);
		} else {
			chmod(dstFile, _S_IWRITE | _S_IREAD);
			chmod(dstTimeStamp, _S_IWRITE | _S_IREAD);
			ret = NO_ERROR;
		}


		if (ret!=NO_ERROR && ret!=GIMME_ERROR_NOT_IN_DB && ret!=GIMME_ERROR_NO_SC && ret!=GIMME_ERROR_NO_DLL)
		{
			Errorf("    Can't checkout %s or the timestamp %s. (%s)\n",dstFile,dstTimeStamp,gimmeDLLGetErrorString(ret));
			if (strstriConst(gimmeDLLGetErrorString(ret), "already deleted")) {
				printfColor(COLOR_RED|COLOR_BRIGHT, "WARNING: \"%s\" has been previously marked as deleted,\n   you will need to manually re-add this file with Gimme Checkin (Checkin All will skip it)\n", dstFile);
			} else {
				// Because of the checks above, if we get here, this file is one that should be processed!
				if (!g_force_rebuild)
					FatalErrorf(".WRL file is owned by you, but .geo file is checked out by someone else!\n%s (%s)\n%s (%s)\n", srcFile, gimmeDLLQueryLastAuthor(srcFile), dstFile, gimmeDLLQueryIsFileLocked(dstFile)?gimmeDLLQueryIsFileLocked(dstFile):gimmeDLLQueryLastAuthor(dstFile));
				loadend_printf("Done.");
				return REASON_CHECKOUTFAILED;
			}
		}

		if (!g_no_checkout)
		{
			if (gimmeDLLQueryIsFileBlocked(dstFile))
				gimmeDLLUnblockFile(dstFile);
			if (gimmeDLLQueryIsFileBlocked(dstTimeStamp))
				gimmeDLLUnblockFile(dstTimeStamp);
		}

		// remove .bak file of the .geo, it may have inadvertently been created from the step
		// above that created an empty file so that it could be checked out
		{
			char bakname[MAX_PATH];
			strcpy(bakname, dstFile);
			strcat(bakname, ".bak");
			fileForceRemove(bakname);
			strcpy(bakname, dstTimeStamp);
			strcat(bakname, ".bak");
			fileForceRemove(bakname);
		}

		return REASON_PROCESSED;
	} else {
		return REASON_NOTNEWER;
	}
}

Reasons findSkelForDanim( char * src_name, char** ppcResult) 
{
	char* pcDirName = strdup(src_name);
	bool bFound = false;
	char* pcPrevName = strdup(src_name); // getDirectoryName can get stuck on a loop at root dir
	(*ppcResult) = NULL;
	while (!bFound && getDirectoryName(pcDirName) && stricmp(pcDirName, pcPrevName) != 0)
	{
		char** ppcFNames;
		int iCount, i;
		int iSkelIndex = -1;
		ppcFNames = fileScanDirNoSubdirRecurse(pcDirName);
        iCount = eaSize( &ppcFNames );
		for (i=0; i<iCount; ++i)
		{
			if (strEndsWith(ppcFNames[i], ".wrl"))
			{
				if (iSkelIndex >= 0)
				{
					Errorf("Too many .wrl's in directory %s, only one skeleton file per anim dir! Unable to process anim %s", pcDirName, src_name);
					free(pcDirName);
					return REASON_INVALID_SRC_DIR;
				}
				iSkelIndex = i;
			}
		}
		if (iSkelIndex >= 0)
		{
			// Found the .skel!
			bFound = true;
			(*ppcResult) = strdup(ppcFNames[iSkelIndex]);
		}
		free(pcPrevName);
		pcPrevName = strdup(pcDirName);
	}
	if (!bFound)
	{
		Errorf("Failed to find any .wrl skeletons for .danim %s", src_name);
		return REASON_INVALID_SRC_DIR;
	}
	free(pcDirName);
	return REASON_PROCESSED;
}

static void dynAnimTrackClear(DynAnimTrack* pAnimTrack)
{
	if (pAnimTrack->bones)
	{
		U32 uiIndex;
		for (uiIndex=0; uiIndex<pAnimTrack->uiBoneCount; ++uiIndex)
		{
			SAFE_FREE(pAnimTrack->bones[uiIndex].posKeyFrames);
			SAFE_FREE(pAnimTrack->bones[uiIndex].rotKeyFrames);
			SAFE_FREE(pAnimTrack->bones[uiIndex].scaleKeyFrames);
			SAFE_FREE(pAnimTrack->bones[uiIndex].posKeys);
			SAFE_FREE(pAnimTrack->bones[uiIndex].rotKeys);
			SAFE_FREE(pAnimTrack->bones[uiIndex].scaleKeys);
		}
		free(pAnimTrack->bones);
		pAnimTrack->bones = NULL;
	}
	if (pAnimTrack->bonesCompressed)
	{
		U32 uiIndex;
		for (uiIndex=0; uiIndex<pAnimTrack->uiBoneCount; ++uiIndex)
		{
			SAFE_FREE(pAnimTrack->bonesCompressed[uiIndex].pStaticTracks);
			if (pAnimTrack->bonesCompressed[uiIndex].pWaveletTracks)
			{
				/*
				int i;
				for (i=0; i<9; ++i)
				{
					SAFE_FREE(pAnimTrack->bonesCompressed[uiIndex].pWaveletTracks[i].zippedCoefs);
				}
				*/
				free(pAnimTrack->bonesCompressed[uiIndex].pWaveletTracks);
			}
		}
		free(pAnimTrack->bonesCompressed);
		pAnimTrack->bonesCompressed = NULL;
	}
	stashTableDestroy(pAnimTrack->boneTable);
	pAnimTrack->boneTable = NULL;
}

Reasons processAnim(const char* fname, bool is_core, bool is_reprocess)
{
	char	src_name[_MAX_PATH];
	char	anim_name[_MAX_PATH];
	char	int_name[_MAX_PATH];
	char* pcSkelFileName;
	DynAnimTrack animTrack = {0};
	const DynBaseSkeleton* pBaseSkel = NULL;
	Reasons reason;
	animTrack.uiTotalFrames = 0;

	makefullpath(fname,src_name); //add cwd to 


	strcpy(int_name, fname);
	{
		char* s;
		s = strstri(int_name, ".DANIM");
		if ( s )
			*s = 0;

		animTrack.pcName = strstri(int_name, pcAnimDir) + strlen(pcAnimDir);
	}

	if (!animSrcToData(src_name, SAFESTR(anim_name), is_core) )
	{
		return REASON_INVALID_SRC_DIR;
	}

	// Find the skel file
	reason = findSkelForDanim(src_name, &pcSkelFileName);

	// See if we can process this file
	if ( reason == REASON_PROCESSED )
		reason = checkIfFileShouldBeProcessed(src_name, anim_name, pcSkelFileName, is_reprocess);
	
	// Now, try and find base skeleton
	if ( reason == REASON_PROCESSED )
	{
		char* pcSkelName = getSkelNameFromSrcName(pcSkelFileName);
		pBaseSkel = dynBaseSkeletonFind(pcSkelName);
		if (!pBaseSkel)
		{
			Errorf("Failed to find base skeleton %s for animation %s", pcSkelName, src_name);
			reason = REASON_INVALID_SRC_DIR;
		}
		free(pcSkelName);

		if ( reason == REASON_PROCESSED )
		{
			U32 uiTotalBones;
			loadstart_printf("Using skeleton %s.. ", pcSkelFileName);
			loadend_printf("Done");

			reason = convertDanimToAnimTrack(src_name, &animTrack, pBaseSkel, iMaxExporterVersion, &uiTotalBones);
			if (reason != REASON_PROCESSED)
			{
				loadend_printf("Failed!\n");
				return reason;
			}

			mkdirtree(anim_name);
			outputAnimTrack(anim_name, &animTrack, uiTotalBones);
			outputTimeStamp(anim_name);

			dynAnimTrackClear(&animTrack);
		}

		loadend_printf("Done");
	}

	return reason;
}

static U32 convertVrmlNodeToDynNode(Node* pVrmlNode, DynNode* pDynNode)
{
	U32 uiTotal = 1;
	Quat qNodeRot;
	
	/*
	if (pVrmlNode->poskeys.count > 0 && 0)
		copyVec3((F32*)pVrmlNode->poskeys.vals, pDynNode->vPos);
	else
		*/

	dynNodeSetPos(pDynNode, (F32*)pVrmlNode->dynPos);

	//pDynNode->vPos[0] = -pDynNode->vPos[0];

	// Keep rotations identity for now, in skeleton
	//unitQuat(pDynNode->qRot);
	if (pVrmlNode->rotkeys.uiCount > 0)
		axisAngleToQuat(&((F32*)pVrmlNode->rotkeys.pvAxisAngle)[0], ((F32*)(pVrmlNode->rotkeys.pvAxisAngle))[3], qNodeRot);
	else
		unitQuat(qNodeRot);
	dynNodeSetRot(pDynNode, qNodeRot);

	pDynNode->pcTag = pVrmlNode->name;

	if ( pVrmlNode->next && boneIsOK(pVrmlNode->next->name))
	{
		pDynNode->pSibling = dynNodeAlloc();
		uiTotal += convertVrmlNodeToDynNode(pVrmlNode->next, pDynNode->pSibling);
	}

	if ( pVrmlNode->child && boneIsOK(pVrmlNode->child->name))
	{
		pDynNode->pChild = dynNodeAlloc();
		pDynNode->pChild->pParent = pDynNode;
		uiTotal += convertVrmlNodeToDynNode(pVrmlNode->child, pDynNode->pChild);
	}

	return uiTotal;
}


static void convertVrmlToSkeleton(const char* input_file_name, DynBaseSkeleton* pSkel)
{
	const char **file_names = NULL;
	Node*	pRoot;
	U32 uiTotalBones;

	//Read Vrml file into tree of root
	loadstart_printf("Reading and converting vrml to skeleton format..");
	eaStackCreate(&file_names, 1);
	eaPush(&file_names, input_file_name);
	pRoot = readVrmlFiles(file_names);
	eaDestroy(&file_names);
	if(!pRoot)
		FatalErrorf("Can't open %s for reading!\n",input_file_name);

	pSkel->pRoot = dynNodeAlloc();
	uiTotalBones = convertVrmlNodeToDynNode(pRoot, pSkel->pRoot);
	loadend_printf(" Done");

}

static void outputSkeleton(char* output_file_name, DynBaseSkeleton* pSkel)
{
	FILE* file;
	loadstart_printf("Writing file %s..", output_file_name);
	file = fopen( output_file_name, "wb" );
	if ( !file )
	{
		FatalErrorf("Failed to open file %s\n", output_file_name);
		loadend_printf("Failed, returning.");
		return;
	}
	// First write length of name, then string
	{
		U32 uiLen = strlen(pSkel->pcName);
		fwrite(&uiLen, sizeof(U32), 1, file);
		fwrite(pSkel->pcName, sizeof(char), uiLen+1, file);
	}

	// Then write the tree
	dynNodeWriteTree(file, pSkel->pRoot);

	// done with the file
	fclose(file);
	loadend_printf(" Done");
}


Reasons processSkeleton(const char* input_file_name, bool is_core, bool is_reprocess)
{
	char output_file_name[_MAX_PATH];
	char skelName[_MAX_PATH];
	DynBaseSkeleton skeleton;
	Reasons reason;


	// Setup output name and skelname
	{
		const char* s;
		char* d;
		s = strstriConst(input_file_name, pcAnimDir) + strlen(pcAnimDir);
		if (is_core)
		{
			const char *core_data_dir = fileCoreDataDir();
			assert(core_data_dir);
			sprintf(output_file_name,"%s/%s/%s",core_data_dir,pcAnimSkeletonDir,s);
		}
		else
		{
			sprintf(output_file_name,"%s/%s/%s",fileDataDir(),pcAnimSkeletonDir,s);
		}

		d = strstri(output_file_name, ".WRL");
		strcpy_s(d, output_file_name + ARRAY_SIZE(output_file_name) - d, ".skel");
		forwardSlashes(output_file_name);

		strcpy(skelName, s);
		d = strstri(skelName, ".WRL");
		if (d)
			*d = 0;
		skeleton.pcName = skelName;

	}

	reason = checkIfFileShouldBeProcessed(input_file_name, output_file_name, NULL, is_reprocess);

	if ( reason == REASON_PROCESSED )
	{
		convertVrmlToSkeleton(input_file_name, &skeleton);

		mkdirtree(output_file_name);
		outputSkeleton(output_file_name, &skeleton);
		outputTimeStamp(output_file_name);

		loadend_printf("Done\n");
	}

	return reason;
}

#include "AutoGen/procAnim_h_ast.c"
