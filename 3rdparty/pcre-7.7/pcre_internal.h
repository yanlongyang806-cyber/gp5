/*************************************************
*      Perl-Compatible Regular Expressions       *
*************************************************/


/* PCRE is a library of functions to support regular expressions whose syntax
and semantics are as close as possible to those of the Perl 5 language.

                       Written by Philip Hazel
           Copyright (c) 1997-2008 University of Cambridge

-----------------------------------------------------------------------------
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

    * Neither the name of the University of Cambridge nor the names of its
      contributors may be used to endorse or promote products derived from
      this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
-----------------------------------------------------------------------------
*/

/* This header contains definitions that are shared between the different
modules, but which are not relevant to the exported API. This includes some
functions whose names all begin with "_pcre_". */

#ifndef PCRE_INTERNAL_H
#define PCRE_INTERNAL_H

/* Define DEBUG to get debugging output on stdout. */

#if 0
#define DEBUG
#endif

/* Use a macro for debugging printing, 'cause that eliminates the use of #ifdef
inline, and there are *still* stupid compilers about that don't like indented
pre-processor statements, or at least there were when I first wrote this. After
all, it had only been about 10 years then...

It turns out that the Mac Debugging.h header also defines the macro DPRINTF, so
be absolutely sure we get our version. */

#undef DPRINTF
#ifdef DEBUG
#define DPRINTF(p) printf p
#else
#define DPRINTF(p) /* Nothing */
#endif


/* Standard C headers plus the external interface definition. The only time
setjmp and stdarg are used is when NO_RECURSE is set. */

#include <ctype.h>
#include <limits.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* When compiling a DLL for Windows, the exported symbols have to be declared
using some MS magic. I found some useful information on this web page:
http://msdn2.microsoft.com/en-us/library/y4h7bcy6(VS.80).aspx. According to the
information there, using __declspec(dllexport) without "extern" we have a
definition; with "extern" we have a declaration. The settings here override the
setting in pcre.h (which is included below); it defines only PCRE_EXP_DECL,
which is all that is needed for applications (they just import the symbols). We
use:

  PCRE_EXP_DECL       for declarations
  PCRE_EXP_DEFN       for definitions of exported functions
  PCRE_EXP_DATA_DEFN  for definitions of exported variables

The reason for the two DEFN macros is that in non-Windows environments, one
does not want to have "extern" beforeRFMT_CODE_ENTITEM || pContainer->chType == STRFMT_CODE_ENTITEMDEF ? pContainer->pValue : NULL));
	else if (!stricmp(pchField, "Description"))
		estrAppend2(ppchResult, langTranslateMessageRef(pContext->langID, pDef->descriptionMsg.hMessage));
	else if (!stricmp(pchField, "ShortDescription"))
		estrAppend2(ppchResult, langTranslateMessageRef(pContext->langID, pDef->descShortMsg.hMessage));
	else if (!stricmp(pchField, "Icon"))
		estrAppend2(ppchResult, pDef->pchIconName);
	else if (!stricmp(pchField, "Tag") && StaticDefineGetMessage(ItemTagEnum, pDef->eTag))
		estrAppend2(ppchResult, langTranslateMessage(pContext->langID, StaticDefineGetMessage(ItemTagEnum, pDef->eTag)));
	else if (!stricmp(pchField, "Tag.Name"))
		estrAppend2(ppchResult, StaticDefineIntRevLookupNonNull(ItemTagEnum, pDef->eTag));
	else if (!stricmp(pchField, "Type") && StaticDefineGetMessage(ItemTypeEnum, pDef->eType))
		estrAppend2(ppchResult, langTranslateMessage(pContext->langID, StaticDefineGetMessage(ItemTypeEnum, pDef->eType)));
	else if (!stricmp(pchField, "Type.Name"))
		estrAppend2(ppchResult, StaticDefineIntRevLookupNonNull(ItemTypeEnum, pDef->eType));
	else if (!stricmp(pchField, "BagSlots"))
		estrAppend2(ppchResult, PrettyPrintInt(pDef->iNumBagSlots, 0));
	else if (!stricmp(pchField, "Quality") && StaticDefineGetMessage(ItemQualityEnum, pDef->Quality))
		// FIXME(jm): The fake item quality message will never return a non-null message.
		estrAppend2(ppchResult, langTranslateMessage(pContext->langID, StaticDefineGetMessage(ItemQualityEnum, pDef->Quality)));
	else if (!stricmp(pchField, "Quality.Name") && StaticDefineGetMessage(ItemQualityEnum, pDef->Quality))
		estrAppend2(ppchResult, StaticDefineIntRevLookupNonNull(ItemQualityEnum, pDef->Quality));
	else if (!stricmp(pchField, "MinLevel"))
		estrAppend2(ppchResult, PrettyPrintInt(SAFE_MEMBER2(pDef, pRestriction, iMinLevel), 0));
	else if (!stricmp(pchField, "MaxLevel"))
		estrAppend2(ppchResult, PrettyPrintInt(SAFE_MEMBER2(pDef, pRestriction, iMaxLevel), 0));
	else if (!stricmp(pchField, "StackLimit"))
		estrAppend2(ppchResult, PrettyPrintInt(pDef->iStackLimit, 0));
	else if (!stricmp(pchField, "SkillType") && StaticDefineGetMessage(SkillTypeEnum, pDef->kSkillType))
		estrAppend2(ppchResult, langTranslateMessage(pContext->langID, StaticDefineGetMessage(SkillTypeEnum, pDef->kSkillType)));
	else if (!stricmp(pchField, "SkillType.Name"))
		estrAppend2(ppchResult, StaticDefineIntRevLookupNonNull(SkillTypeEnum, pDef->kSkillType));
	else if (!stricmp(pchField, "Bag1") && StaticDefineGetMessage(InvBagIDsEnum, eaiGet(&pDef->peRestrictBagIDs,0)))
		estrAppend2(ppchResult, langTranslateMessage(pContext->langID, StaticDefineGetMessage(InvBagIDsEnum, eaiGet(&pDef->peRestrictBagIDs,0))));
	else if (!stricmp(pchField, "Bag1.Name"))
		estrAppend2(ppchResult, StaticDefineIntRevLookupNonNull(InvBagIDsEnum, eaiGet(&pDef->peRestrictBagIDs,0)));
	else if (!stricmp(pchField, "Bag2") && StaticDefineGetMessage(InvBagIDsEnum, eaiGet(&pDef->peRestrictBagIDs,1)))
		estrAppend2(ppchResult, langTranslateMessage(pContext->langID, StaticDefineGetMessage(InvBagIDsEnum, eaiGet(&pDef->peRestrictBagIDs,1))));
	else if (!stricmp(pchField, "Bag2.Name"))
		estrAppend2(ppchResult, StaticDefineIntRevLookupNonNull(InvBagIDsEnum, eaiGet(&pDef->peRestrictBagIDs,1)));
	else if (strStartsWith(pchField, "Bag["))
	{
		int len = (int)strcspn(pchField+4, "]");
		char* pchBuffer = alloca(len+1);
		strncpy_s(pchBuffer, len+1, pchField+4, len);
		if (strIsNumeric(pchBuffer))
		{
			InvBagIDs eBagID = eaiGet(&pDef->peRestrictBagIDs, atoi(pchBuffer));
			if (strEndsWith(pchField, ".Name"))
				estrAppend2(ppchResult, StaticDefineIntRevLookupNonNull(InvBagIDsEnum, eBagID));
			else
				estrAppend2(ppchResult, langTranslateMessage(pContext->langID, StaticDefineGetMessage(InvBagIDsEnum, eBagID)));
		}
	}
	else if (!stricmp(pchField, "Slot") && StaticDefineGetMessage(SlotTypeEnum, pDef->eRestrictSlotType))
		estrAppend2(ppchResult, langTranslateMessage(pContext->langID, StaticDefineGetMessage(SlotTypeEnum, pDef->eRestrictSlotType)));
	else if (!stricmp(pchField, "Slot.Name"))
		estrAppend2(ppchResult, StaticDefineIntRevLookupNonNull(SlotTypeEnum, pDef->eRestrictSlotType));
	else if (strStartsWith(pchField, "Value"))
		return EntItemDefValueFormatField(ppchResult, pContainer, NULL, pDef, pchField + 5, pContext);
	else if (strStartsWith(pchField, "Recipe") || strStartsWith(pchField, "Craft"))
	{
		ItemCraftingTable *pCraft = pDef->pCraft;
		if (pCraft && strStartsWith(pchField, "Recipe"))
			return ItemCraftingTableFormatField(ppchResult, pContainer, pCraft, pchField + 6, pContext);
		else if (pCraft)
		{
			// TODO(jm): deprecate the 2 uses in Champions.
			ErrorFilenamef(s_pchFilename, "Uses old {Item.Craft...}, it should get updated to {Item.Recipe...}.");
			return ItemCraftingTableFormatField(ppchResult, pContainer, pCraft, pchField + 5, pContext);
		}
		else
			return true;
	}
	else if (strStartsWith(pchField, "Restriction"))
	{
		UsageRestriction *pRestriction = pDef->pRestriction;
		if (pRestriction)
			return UsageRestrictionFormatField(ppchResult, pContainer, pRestriction, pchField + 11, pContext);
		else
			return true;
	}
	else if (strStartsWith(pchField, "Mission"))
	{
		MissionDef *pMissionDef = GET_REF(pDef->hMission);
		if (pMissionDef)
			return MissionDefFormatField(ppchResult, pContainer, pMissionDef, pchField + 7, pContext);
		else
			return true;
	}
	else
		StringFormatErrorReturn(pchField, "ItemDef");
	return true;
}

static bool ItemDefConditionField(StrFmtContainer *pContainer, ItemDef *pItemDef, const unsigned char *pchField, StrFmtContext *pContext)
{
	const char *pch;
	STRIP_LEADING_DOTS(pchField);
	if (!pItemDef)
		return false;
	else if (!*pchField)
		return true;
	else if (strStartsWith(pchField, "Name"))
		return (pch = langTranslateDisplayMessage(pContext->langID, pItemDef->displayNameMsgUnidentified)) != NULL && *pch != '\0';
	else if (strStartsWith(pchField, "Description"))
		return (pch = langTranslateDisplayMessage(pContext->langID, pItemDef->descriptionMsg)) != NULL && *pch != '\0';
	else if (strStartsWith(pchField, "ShortDescription"))
		return (pch = langTranslateDisplayMessage(pContext->langID, pItemDef->descShortMsg)) != NULL && *pch != '\0';
	else if (strStartsWith(pchField, "Type"))
		return StaticDefineIntConditionField(pContainer, pItemDef->eType, ItemTypeEnum, pchField + 4, pContext);
	else if (strStartsWith(pchField, "Quality"))
		return StaticDefineIntConditionField(pContainer, pItemDef->Quality, ItemQualityEnum, pchField + 7, pContext);
	else if (strStartsWith(pchField, "Flags"))
		return ItemDefFlagsConditionField(pContainer, pItemDef, pchField + 5, pContext);
	else if (strStartsWith(pchField, "SkillType"))
		return StaticDefineIntConditionField(pContainer, pItemDef->kSkillType, SkillTypeEnum, pchField + 9, pContext);
	else if (!stricmp(pchField, "MissionGrant"))
		return item_IsMissionGrant(pItemDef);
	else if (!stricmp(pchField, "MissionItem"))
		return item_IsMission(pItemDef);
	else if (strStartsWith(pchField, "MinLevel"))
		return strfmt_NumericCondition(SAFE_MEMBER2(pItemDef, pRestriction, iMinLevel), pchField + 8, s_pchFilename);
	else if (strStartsWith(pchField, "MaxLevel"))
		return strfmt_NumericCondition(SAFE_MEMBER2(pItemDef, pRestriction, iMaxLevel), pchField + 8, s_pchFilename);
	else if (strStartsWith(pchField, "StackLimit"))
		return strfmt_NumericCondition(pItemDef->iStackLimit, pchField + 10, s_pchFilename);
	else if (!stricmp(pchField, "CostumeUnlock"))
		return pItemDef->eCostumeMode == kCostumeDisplayMode_Unlock && eaSize(&pItemDef->ppCostumes) > 0;
	else if (!stricmp(pchField, "Recipe"))
		return item_IsRecipe(pItemDef);
	else if (strStartsWith(pchField, "Tag"))
		return StaticDefineIntConditionField(pContainer, pItemDef->eTag, ItemTagEnum, pchField + 3, pContext);
	else if (strStartsWith(pchField, "Recipe"))
		return ItemCraftingTableConditionField(pContainer, pItemDef->pCraft, pchField + 6, pContext);
	else if (strStartsWith(pchField, "Restriction"))
		return UsageRestrictionConditionField(pContainer, pItemDef->pRestriction, pchField + 11, pContext);
	else if (strStartsWith(pchField, "BagSlots"))
		return strfmt_NumericCondition(pItemDef->iNumBagSlots, pchField + 8, s_pchFilename);
	else if (strStartsWith(pchField, "Bag1")) 
		return StaticDefineIntConditionField(pContainer, eaiGet(&pItemDef->peRestrictBagIDs,0), InvBagIDsEnum, pchField + 4, pContext);
	else if (strStartsWith(pchField, "Bag2"))
		return StaticDefineIntConditionField(pContainer, eaiGet(&pItemDef->peRestrictBagIDs,1), InvBagIDsEnum, pchField + 4, pContext);
	else if (strStartsWith(pchField, "Bag["))
	{
		int len = (int)strcspn(pchField+4, "]");
		char* pchBuffer = alloca(len+1);
		strncpy_s(pchBuffer, len+1, pchField+4, len);
		if (strIsNumeric(pchBuffer))
		{
			InvBagIDs eBagID = eaiGet(&pItemDef->peRestrictBagIDs, atoi(pchBuffer));
			return StaticDefineIntConditionField(pContainer, eBagID, InvBagIDsEnum, pchField + len + 5, pContext);
		}
	}
	else if (strStartsWith(pchField, "Slot"))
		return StaticDefineIntConditionField(pContainer, pItemDef->eRestrictSlotType, SlotTypeEnum, pchField + 4, pContext);
	else if (strStartsWith(pchField, "Value"))
		return EntItemDefValueConditionField(pContainer, NULL, pItemDef, pchField + 5, pContext);
	StringConditionErrorReturn(pchField, "ItemDef");
}

static bool EntItemDefConditionField(StrFmtContainer *pContainer, Entity *pEntity, const unsigned char *pchField, StrFmtContext *pContext)
{
	Item *pItem = pContainer->chType == STRFMT_CODE_ENTITEM ? pContainer->pValue2 : NULL;
	ItemDef *pItemDef = pContainer->chType == STRFMT_CODE_ENTITEMDEF ? pContainer->pValue2 : pItem ? GET_REF(pItem->hItem) : NULL;
	const char *pchUsage = pContainer->pchValue;
	int i;

	STRIP_LEADING_DOTS(pchField);
	if (!pItemDef)
		return false;
	else if (!*pchField)
		return true;
	else if (!pEntity)
		return ItemDefConditionField(pContainer, pItemDef, pchField, pContext);
	else if (strStartsWith(pchField, "Value"))
		return EntItemDefValueConditionField(pContainer, pEntity, pItemDef, pchField + 5, pContext);
	else if (!stricmp(pchField, "Recipe.New"))
	{
		GameAccountDataExtract *pExtract;
		bool bResult;

		if (!item_IsRecipe(pItemDef))
			return false;

		pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
		bResult = inv_ent_CountItems(pEntity, InvBagIDs_Recipe, pItemDef->pchName, pExtract) == 0;
		return bResult;
	}
	else if (!stricmp(pchField, "MissionGrant.New"))
	{
		if (!item_IsMissionGrant(pItemDef))
			return false;
		return true;
	}
	else if (!stricmp(pchField, "CostumeUnlock.New"))
	{
		SavedEntityData *pSaved = SAFE_MEMBER(pEntity, pSaved);
		GameAccountData *pAccountData = entity_GetGameAccount(pEntity);
		if (pItemDef->eCostumeMode != kCostumeDisplayMode_Unlock)
			return false;
		if (!pEntity->pSaved)
			return true;
		if (pItem && (pItem->flags & kItemFlag_Algo) && pItem->pSpecialProps)
		{
			return !costumeEntity_IsUnlockedCostumeRef(pSaved->costumeData.eaUnlockedCostumeRefs, pAccountData, pEntity, pEntity, REF_STRING_FROM_HANDLE(pItem->pSpecialProps->hCostumeRef));
		}
		else
		{
			for (i = eaSize(&pItemDef->ppCostumes) - 1; i >= 0; i--)
				if (!costumeEntity_IsUnlockedCostumeRef(pSaved->costumeData.eaUnlockedCostumeRefs, pAccountData, pEntity, pEntity, REF_STRING_FROM_HANDLE(pItemDef->ppCostumes[i]->hCostumeRef)))
					return true;
		}
		return false;
	}
	else if (strStartsWith(pchField, "Restriction"))
		return EntUsageRestrictionConditionField(pContainer, pEntity, pItem, pItemDef, pItemDef->pRestriction, pchField + 11, pContext);
	else
		return ItemDefConditionField(pContainer, pItemDef, pchField, pContext);
	StringConditionErrorReturn(pchField, "EntItemDef");
}

static bool ItemDescriptionFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, Item *pItem, const unsigned char *pchField, StrFmtContext *pContext)
{
	int i;
	ItemDef *pDef = GET_REF(pItem->hItem);
	bool bShowItemDesc = true;
	bool bShowPowerDesc = (pItem->flags & kItemFlag_Algo) && pDef && item_IsUniden;
	hullCopy(editState.debugInfo.activeHull, partition->hull);
	eaiCopy(&editState.debugInfo.activeTriToPlane, &partition->tri_to_plane);
}

#endif                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         SVN   ‚†‚†€‚†//{{NO_DEPENDENCIES}}
// Microsoft Visual C++ generated include file.
// Used by MasterControlProgram1.rc
//
#define ID_GETFROMSECS2                 2
#define IDCLEARMEMLEAK                  2
#define IDSVNCHECKINS                   3
#define IDTEXTTOSUPESC                  3
#define IDPURGELOGS                     4
#define IDSUPESCTOTEXT                  5
#define IDXBOXCP                        5
#define IDTEXTTOTP                      6
#define IDSENTRYSERVERTEST              6
#define IDTPTOTEXT                      7
#define IDMEMLEAKS                      7
#define IDTEXTTOESC                     8
#define IDTIMINGCONVERSION              8
#define IDESCTOTEXT                     9
#define IDD_DIALOG1                     101
#define IDD_BGB_COMPLEX                 101
#define IDD_DIALOG2                     104
#define IDD_MCP_START                   104
#define IDD_BGB_SIMPLE                  105
#define IDB_BITMAP1                     108
#define IDB_BIGGREENBUTTON              108
#define IDB_BIGGREENBUTTONDOWN          110
#define IDB_BITMAP2                     114
#define IDD_EDITCOMMANDLINE             115
#define IDD_ARTISTSETUP                 116
#define IDD_BGB_ARTISTS                 117
#define IDB_BLACK                       119
#define IDB_EXCLAMATIONPOINT            120
#define IDB_ARROW                       121
#define IDB_SMALLEXCLAMATIONPOINT       122
#define IDD_OTHEROPTIONS                123
#define IDI_ICON1                       124
#define IDD_QUITMESSAGE                 125
#define IDD_CONTROLLERSCRIPTS           126
#define IDB_BITMAP4                     127
#define IDB_LOADING                     127
#define IDB_LOSTWORLDS                  128
#define IDB_LOSTWORLDSDOWN              129
#define IDB_LOSTWORLDSLOADING           130
#define IDI_LOSTWORLDS                  131
#define IDB_BITMAP3                     135
#define IDB_SCRIPTCOMPLETEWERRORS       137
#define IDB_SCRIPTFAILED                138
#define IDB_SCRIPTSUCCEEDED             139
#define IDD_SWMTESTMAIN                 140
#define IDD_SWMTESTSUB                  141
#define IDD_GENERICMESSAGE              142
#define IDD_SUPERESCAPER                143
#define IDD_UTILITIES                   145
#define IDD_GETGIMMECHECKINS            146
#define IDD_GETSVNCHECKINS              147
#define IDD_ACCOUNTINFO                 148
#define IDD_PURGELOGFILES               150
#define IDD_XBOX_CONTROL_PANEL          151
#define IDD_SENTRYSERVERTESTER          152
#define IDD_DIALOG3                     153
#define IDD_SERVERMONCONTROL            153
#define IDD_MEMLEAKFINDER               154
#define MCP_SERVER_SETUP_TXT            155
#define IDD_CONFIG                      157
#define IDR_TXT1                        159
#define MEMLEAKCATEGORIES_TXT           159
#define IDD_ERRORS                      160
#define IDD_TIMECONVERSION              161
#define IDC_BUTTON1                     1001
#define IDC_BIGGREENBUTTON              1001
#define IDC_RUNLOCAL                    1001
#define IDC_LAUNCHGETANIM               1001
#define IDC_TRANSSERVERHIDDEN           1002
#define IDC_LAUNCHGETTEXTURES           1002
#define IDC_BUTTON2                     1002
#define IDC_TRANSSERVERDEBUG            1003
#define IDC_LAUNCHGETPLAYERGEOM         1003
#define IDC_BUTTON3                     1003
#define IDC_OBJECTDBHIDDEN              1004
#define IDC_LAUNCHGETOBJECTLIBRARY      1004
#define IDC_BUTTON4                     1004
#define IDC_OBJECTDBDEBUG               1005
#define IDC_BUTTON5                     1005
#define IDC_RESET                       1006
#define IDC_BUTTON6                     1006
#define IDC_CONTROLLERHIDDEN            1007
#define IDC_BUTTON7                     1007
#define IDC_GAMESERVERHIDDEN            1008
#define IDC_BUTTON8                     1008
#define IDC_GAMESERVERDEBUG             1009
#define IDC_BUTTON9                     1009
#define IDC_GAMECLIENTHIDDEN            1010
#define IDC_BUTTON10                    1010
#define IDC_GAMECLIENTDEBUG             1011
#define IDC_BUTTON11                    1011
#define IDC_APPSERVERHIDDEN             1012
#define IDC_LOGINSERVERHIDDEN           1012
#define IDC_BUTTON44                    1012
#define IDC_SERVERMONSTAT6              1012
#define IDC_APPSERVERDEBUG              1013
#define IDC_LOGINSERVERDEBUG            1013
#define IDC_BUTTON12                    1013
#define IDC_CONTROLLERDEBUG             1014
#define IDC_BUTTON13                    1014
#define IDC_LAUNCHERHIDDEN              1015
#define IDC_BUTTON14                    1015
#define IDC_CHOOSESERVER                1016
#define IDC_LAUNCHERDEBUG               1016
#define IDC_BUTTON15                    1016
#define IDC_RUNPUBLICLOCAL              1017
#define IDC_TRANSSERVERNOAUTOLAUNCH     1017
#define IDC_BUTTON16                    1017
#define IDC_MULTIPLEXERHIDDEN           1017
#define IDC_STARTINGGSMAP               1017
#define IDC_OBJECTDBNOAUTOLAUNCH        1018
#define IDC_BUTTON17                    1018
#define IDC_LAUNCHERDEBUG2              1018
#define IDC_MULTIPLEXERDEBUG            1018
#define IDC_CONNECTTOSERVER             1019
#define IDC_GAMESERVERNOAUTOLAUNCH      1019
#define IDC_BUTTON18                    1019
#define IDC_PATCHXBOX                   1019
#define IDC_GAMECLIENTNOAUTOLAUNCH      1020
#define IDC_BUTTON19                    1020
#define IDC_LAUNCHCLIENT                1020
#define IDC_TRANSSERVER_BUTTON          1021
#define IDC_BUTTON20                    1021
#define IDC_PATCHPC                     1021
#define IDC_OBJECTDB_BUTTON             1022
#define IDC_COMPLEXMODE                 1022
#define IDC_BUTTON21                    1022
#define IDC_GAMESERVER_BUTTON           1023
#define IDC_BUTTON22                    1023
#define IDC_GAMECLIENT_BUTTON           1024
#define IDC_BUTTON23                    1024
#define IDC_APPSERVER_BUTTON            1025
#define IDC_LOGINSERVER_BUTTON          1025
#define IDC_BUTTON24                    1025
#define IDC_APPSERVER_STATUS            1025
#define IDC_SIMPLEMODE                  1026
#define IDC_BUTTON25                    1026
#define IDC_APPSERVERNOAUTOLAUNCH       1027
#define IDC_LOGINSERVERNOAUTOLAUNCH     1027
#define IDC_BUTTON26                    1027
#define IDC_MAPMANAGERHIDDEN            1028
#define IDC_BUTTON27                    1028
#define IDC_CONTAINER_HIDDEN            1028
#define IDC_TRANSSERVERSTATUSTEXT       1029
#define IDC_BUTTON28                    1029
#define IDC_CONTAINER_HIDDEN2           1029
#define IDC_OBJECTDBSTATUSTEXT          1030
#define IDC_BUTTON29                    1030
#define IDC_CONTAINER_HIDDEN3           1030
#define IDC_GAMESERVERSTATUSTEXT        1031
#define IDC_BUTTON30                    1031
#define IDC_CONTAINER_HIDDEN4           1031
#define IDC_GAMECLIENTSTATUSTEXT        1032
#define IDC_BUTTON31                    1032
#define IDC_CONTAINER_HIDDEN5           1032
#define IDC_APPSERVERSTATUSTEXT         1033
#define IDC_LOGINSERVERSTATUSTEXT       1033
#define IDC_BUTTON32                    1033
#define IDC_CONTAINER_HIDDEN6           1033
#define IDC_EDITCOMMANDLINE             1034
#define IDC_MAPMANAGERDEBUG             1034
#define IDC_BUTTON33                    1034
#define IDC_CONTAINER_DEBUG             1034
#define IDC_TRANSSERVER_EDITCOMMANDLINE 1035
#define IDC_CONTAINER_DEBUG2            1035
#define IDC_UNESCAPED                   1035
#define IDC_INSTRING                    1035
#define IDC_EDITCOMMANDLINE_CAPTION     1036
#define IDC_OBJECTDB_EDITCOMMANDLINE    1036
#define IDC_CONTAINER_DEBUG3            1036
#define IDC_GAMESERVER_EDITCOMMANDLINE  1037
#define IDC_CONTAINER_DEBUG4            1037
#define IDC_GAMECLIENT_EDITCOMMANDLINE  1038
#define IDC_CONTAINER_DEBUG5            1038
#define IDC_APPSERVER_EDITCOMMANDLINE   1039
#define IDC_LOGINSERVER_EDITCOMMANDLINE 1039
#define IDC_CONTAINER_DEBUG6            1039
#define IDC_SVN   ‡b‡b€‡b// The to be #defined in a .h file included by a .rc file before maxversion.r


#define MAXVER_INTERNALNAME "animexp\0"//should  be overidden on a per-dll level
#define MAXVER_ORIGINALFILENAME "animexp.dle\0"//should  be overidden on a per-dll level
#define MAXVER_FILEDESCRIPTION "Anim file exporter (plugin)\0"//should  be overidden on a per-dll level
#define MAXVER_COMMENTS "Comments\0"//should  be overidden on a per-dll level

// #define MAXVER_PRODUCTNAME //generally not overridden at the maxversion.r level
// #define MAXVER_COPYRIGHT //only in exceptions should this be overridden
// #define MAXVER_LEGALTRADEMARKS //only in exceptions should this be overridden
// #define MAXVER_COMPANYNAME //only in exceptions should this be overridden
// #define MAX_VERSION_MAJOR //only in exceptions should this be overridden
// #define MAX_VERSION_MINOR //only in exceptions should this be overridden
// #define MAX_VERSION_POINT //only in exceptions should this be overridden

                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                s - only for chars < 256                   */ \
  2, 2, 2, 2, 2, 2,              /* NOT *, *?, +, +?, ?, ??                */ \
  4, 4, 4,                       /* NOT upto, minupto, exact               */ \
  2, 2, 2, 4,                    /* Possessive *, +, ?, upto               */ \
  /* Positive type repeats                                                 */ \
  2, 2, 2, 2, 2, 2,              /* Type *, *?, +, +?, ?, ??               */ \
  4, 4, 4,                       /* Type upto, minupto, exact              */ \
  2, 2, 2, 4,                    /* Possessive *+, ++, ?+, upto+           */ \
  /* Character class & ref repeats                                         */ \
  1, 1, 1, 1, 1, 1,              /* *, *?, +, +?, ?, ??                    */ \
  5, 5,                          /* CRRANGE, CRMINRANGE                    */ \
 33,                             /* CLASS                                  */ \
 33,                             /* NCLASS                                 */ \
  0,                             /* XCLASS - variable length               */ \
  3,                             /* REF                                    */ \
  1+LINK_SIZE,                   /* RECURSE                                */ \
  2+2*LINK_SIZE,                 /* CALLOUT                                */ \
  1+LINK_SIZE,                   /* Alt                                    */ \
  1+LINK_SIZE,                   /* Ket                                    */ \
  1+LINK_SIZE,                   /* KetRmax                                */ \
  1+LINK_SIZE,                   /* KetRmin                                */ \
  1+LINK_SIZE,                   /* Assert                                 */ \
  1+LINK_SIZE,                   /* Assert not                             */ \
  1+LINK_SIZE,                   /* Assert behind                          */ \
  1+LINK_SIZE,                   /* Assert behind not                      */ \
  1+LINK_SIZE,                   /* Reverse                                */ \
  1+LINK_SIZE,                   /* ONCE                                   */ \
  1+LINK_SIZE,                   /* BRA                                    */ \
  3+LINK_SIZE,                   /* CBRA                                   */ \
  1+LINK_SIZE,                   /* COND                                   */ \
  1+LINK_SIZE,                   /* SBRA                                   */ \
  3+LINK_SIZE,                   /* SCBRA                                  */ \
  1+LINK_SIZE,                   /* SCOND                                  */ \
  3,                             /* CREF                                   */ \
  3,                             /* RREF                                   */ \
  1,                             /* DEF                                    */ \
  1, 1,                          /* BRAZERO, BRAMINZERO                    */ \
  1, 1, 1, 1,                    /* PRUNE, SKIP, THEN, COMMIT,             */ \
  1, 1, 1                        /* FAIL, ACCEPT, SKIPZERO                 */


/* A magic value for OP_RREF to indicate the "any recursion" condition. */

#define RREF_ANY  0xffff

/* Error code numbers. They are given names so that they can more easily be
tracked. */

enum { ERR0,  ERR1,  ERR2,  ERR3,  ERR4,  ERR5,  ERR6,  ERR7,  ERR8,  ERR9,
       ERR10, ERR11, ERR12, ERR13, ERR14, ERR15, ERR16, ERR17, ERR18, ERR19,
       ERR20, ERR21, ERR22, ERR23, ERR24, ERR25, ERR26, ERR27, ERR28, ERR29,
       ERR30, ERR31, ERR32, ERR33, ERR34, ERR35, ERR36, ERR37, ERR38, ERR39,
       ERR40, ERR41, ERR42, ERR43, ERR44, ERR45, ERR46, ERR47, ERR48, ERR49,
       ERR50, ERR51, ERR52, ERR53, ERR54, ERR55, ERR56, ERR57, ERR58, ERR59,
       ERR60, ERR61, ERR62, ERR63, ERR64 };

/* The real format of the start of the pcre block; the index of names and the
code vector run on as long as necessary after the end. We store an explicit
offset to the name table so that if a regex is compiled on one host, saved, and
then run on another where the size of pointers is different, all might still
be well. For the case of compiled-on-4 and run-on-8, we include an extra
pointer that is always NULL. For future-proofing, a few dummy fields were
originally included - even though you can never get this planning right - but
there is only one left now.

NOTE NOTE NOTE:
Because people can now save and re-use compiled patterns, any additions to this
structure should be made at the end, and something earlier (e.g. a new
flag in the options or one of the dummy fields) should indicate that the new
fields are present. Currently PCRE always sets the dummy fields to zero.
NOTE NOTE NOTE:
*/

typedef struct real_pcre {
  pcre_uint32 magic_number;
  pcre_uint32 size;               /* Total that was malloced */
  pcre_uint32 options;            /* Public options */
  pcre_uint16 flags;              /* Private flags */
  pcre_uint16 dummy1;             /* For future use */
  pcre_uint16 top_bracket;
  pcre_uint16 top_backref;
  pcre_uint16 first_byte;
  pcre_uint16 req_byte;
  pcre_uint16 name_table_offset;  /* Offset to name table that follows */
  pcre_uint16 name_entry_size;    /* Size of any name items */
  pcre_uint16 name_count;         /* Number of name items */
  pcre_uint16 ref_count;          /* Reference count */

  const unsigned char *tables;    /* Pointer to tables or NULL for std */
  const unsigned char *nullpad;   /* NULL padding */
} real_pcre;

/* The format of the block used to store data from pcre_study(). The same
remark (see NOTE above) about extending this structure applies. */

typedef struct pcre_study_data {
  pcre_uint32 size;               /* Total that was malloced */
  pcre_uint32 options;
  uschar start_bits[32];
} pcre_study_data;

/* Structure for passing "static" information around between the functions
doing the compiling, so that they are thread-safe. */

typedef struct compile_data {
  const uschar *lcc;            /* Points to lower casing table */
  const uschar *fcc;            /* Points to case-flipping table */
  const uschar *cbits;          /* Points to character type table */
  const uschar *ctypes;         /* Points to table of type maps */
  const uschar *start_workspace;/* The start of working space */
  const uschar *start_code;     /* The start of the compiled code */
  const uschar *start_pattern;  /* The start of the pattern */
  const uschar *end_pattern;    /* The end of the pattern */
  uschar *hwm;                  /* High watermark of workspace */
  uschar *name_table;           /* The name/number table */
  int  names_found;             /* Number of entries so far */
  int  name_entry_size;         /* Size of each entry */
  int  bracount;                /* Count of capturing parens as we compile */
  int  final_bracount;          /* Saved value after first pass */
  int  top_backref;             /* Maximum back reference */
  unsigned int backref_map;     /* Bitmap of low back refs */
  int  external_options;        /* External (initial) options */
  int  external_flags;          /* External flag bits to be set */
  int  req_varyopt;             /* "After variable item" flag for reqbyte */
  BOOL had_accept;              /* (*ACCEPT) encountered */
  int  nltype;                  /* Newline type */
  int  nllen;                   /* Newline string length */
  uschar nl[4];                 /* Newline string when fixed length */
} compile_data;

/* Structure for maintaining a chain of pointers to the currently incomplete
branches, for testing for left recursion. */

typedef struct branch_chain {
  struct branch_chain *outer;
  uschar *current;
} branch_chain;

/* Structure for items in a linked list that represents an explicit recursive
call within the pattern. */

typedef struct recursion_info {
  struct recursion_info *prevrec; /* Previous recursion record (or NULL) */
  int group_num;                /* Number of group that was called */
  const uschar *after_call;     /* "Return value": points after the call in the expr */
  USPTR save_start;             /* Old value of mstart */
  int *offset_save;             /* Pointer to start of saved offsets */
  int saved_max;                /* Number of saved offsets */
} recursion_info;

/* Structure for building a chain of data for holding the values of the subject
pointer at the start of each subpattern, so as to detect when an empty string
has been matched by a subpattern - to break infinite loops. */

typedef struct eptrblock {
  struct eptrblock *epb_prev;
  USPTR epb_saved_eptr;
} eptrblock;


/* Structure for passing "static" information around between the functions
doing traditional NFA matching, so that they are thread-safe. */

typedef struct match_data {
  unsigned long int match_call_count;      /* As it says */
  unsigned long int match_limit;           /* As it says */
  unsigned long int match_limit_recursion; /* As it says */
  int   *offset_vector;         /* Offset vector */
  int    offset_end;            /* One past the end */
  int    offset_max;            /* The maximum usable for return data */
  int    nltype;                /* Newline type */
  int    nllen;                 /* Newline string length */
  uschar nl[4];                 /* Newline string when fixed */
  const uschar *lcc;            /* Points to lower casing table */
  const uschar *ctypes;         /* Points to table of type maps */
  BOOL   offset_overflow;       /* Set if too many extractions */
  BOOL   notbol;                /* NOTBOL flag */
  BOOL   noteol;                /* NOTEOL flag */
  BOOL   utf8;                  /* UTF8 flag */
  BOOL   jscript_compat;        /* JAVASCRIPT_COMPAT flag */
  BOOL   endonly;               /* Dollar not before final \n */
  BOOL   notempty;              /* Empty string match not wanted */
  BOOL   partial;               /* PARTIAL flag */
  BOOL   hitend;                /* Hit the end of the subject at some point */
  BOOL   bsr_anycrlf;           /* \R is just any CRLF, not full Unicode */
  const uschar *start_code;     /* For use when recursing */
  USPTR  start_subject;         /* Start of the subject string */
  USPTR  end_subject;           /* End of the subject string */
  USPTR  start_match_ptr;       /* Start of matched string */
  USPTR  end_match_ptr;         /* Subject position at end match */
  int    end_offset_top;        /* Highwater mark at end of match */
  int    capture_last;          /* Most recent capture number */
  int    start_offset;          /* The start offset value */
  eptrblock *eptrchain;         /* Chain of eptrblocks for tail recursions */
  int    eptrn;                 /* Next free eptrblock */
  recursion_info *recursive;    /* Linked list of recursion data */
  void  *callout_data;          /* To pass back to callouts */
} match_data;

/* A similar structure is used for the same purpose by the DFA matching
functions. */

typedef struct dfa_match_data {
  const uschar *start_code;     /* Start of the compiled pattern */
  const uschar *start_subject;  /* Start of the subject string */
  const uschar *end_subject;    /* End of subject string */
  const uschar *tables;         /* Character tables */
  int   moptions;               /* Match options */
  int   poptions;               /* Pattern options */
  int    nltype;                /* Newline type */
  int    nllen;                 /* Newline string length */
  uschar nl[4];                 /* Newline string when fixed */
  void  *callout_data;          /* To pass back to callouts */
} dfa_match_data;

/* Bit definitions for entries in the pcre_ctypes table. */

#define ctype_space   0x01
#define ctype_letter  0x02
#define ctype_digit   0x04
#define ctype_xdigit  0x08
#define ctype_word    0x10   /* alphanumeric or '_' */
#define ctype_meta    0x80   /* regexp meta char or zero (end pattern) */

/* Offsets for the bitmap tables in pcre_cbits. Each table contains a set
of bits for a class map. Some classes are built by combining these tables. */

#define cbit_space     0      /* [:space:] or \s */
#define cbit_xdigit   32      /* [:xdigit:] */
#define cbit_digit    64      /* [:digit:] or \d */
#define cbit_upper    96      /* [:upper:] */
#define cbit_lower   128      /* [:lower:] */
#define cbit_word    160      /* [:word:] or \w */
#define cbit_graph   192      /* [:graph:] */
#define cbit_print   224      /* [:print:] */
#define cbit_punct   256      /* [:punct:] */
#define cbit_cntrl   288      /* [:cntrl:] */
#define cbit_length  320      /* Length of the cbits table */

/* Offsets of the various tables from the base tables pointer, and
total length. */

#define lcc_offset      0
#define fcc_offset    256
#define cbits_offset  512
#define ctypes_offset (cbits_offset + cbit_length)
#define tables_length (ctypes_offset + 256)

/* Layout of the UCP type table that translates property names into types and
codes. Each entry used to point directly to a name, but to reduce the number of
relocations in shared libraries, it now has an offset into a single string
instead. */

typedef struct {
  pcre_uint16 name_offset;
  pcre_uint16 type;
  pcre_uint16 value;
} ucp_type_table;


/* Internal shared data tables. These are tables that are used by more than one
of the exported public functions. They have to be "external" in the C sense,
but are not part of the PCRE public API. The data for these tables is in the
pcre_tables.c module. */

extern const int    _pcre_utf8_table1[];
extern const int    _pcre_utf8_table2[];
extern const int    _pcre_utf8_table3[];
extern const uschar _pcre_utf8_table4[];

extern const int    _pcre_utf8_table1_size;

extern const char   _pcre_utt_names[];
extern const ucp_type_table _pcre_utt[];
extern const int _pcre_utt_size;

extern const uschar _pcre_default_tables[];

extern const uschar _pcre_OP_lengths[];


/* Internal shared functions. These are functions that are used by more than
one of the exported public functions. They have to be "external" in the C
sense, but are not part of the PCRE public API. */

extern BOOL         _pcre_is_newline(const uschar *, int, const uschar *,
                      int *, BOOL);
extern int          _pcre_ord2utf8(int, uschar *);
extern real_pcre   *_pcre_try_flipped(const real_pcre *, real_pcre *,
                      const pcre_study_data *, pcre_study_data *);
extern int          _pcre_ucp_findprop(const unsigned int, int *, int *);
extern unsigned int _pcre_ucp_othercase(const unsigned int);
extern int          _pcre_valid_utf8(const uschar *, int);
extern BOOL         _pcre_was_newline(const uschar *, int, const uschar *,
                      int *, BOOL);
extern BOOL         _pcre_xclass(int, const uschar *);

#endif

/* End of pcre_internal.h */
