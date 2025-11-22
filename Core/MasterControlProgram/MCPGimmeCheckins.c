
#include "mastercontrolprogram.h"
#include "gimmeutils.h"
#include "timing.h"
#include "Estring.h"
#include "Earray.h"
#include "Svnutils.h"
#include "GlobalTypes.h"
#include "stringUtil.h"
#include "utils.h"
#include "winutil.h"
#include "UTF8.h"


static WNDPROC orig_OutputProc = NULL;


// Superclass the checkin window edit box
static LRESULT CALLBACK MakeCtrlAWorkProc(HWND hWnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	switch( iMsg )
	{

	// Ctrl-A key combinations
	case WM_KEYDOWN:
		if (GetAsyncKeyState(VK_CONTROL))
		{
			switch (wParam)
			{
				case 'A':
					SendMessage(hWnd, EM_SETSEL, 0, -1);
					return true;
			}
		}
	}


	return CallWindowProc(orig_OutputProc, hWnd, iMsg, wParam, lParam)	;
}

BOOL gimmeCheckinsMenuDlgProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	char tempStr[16];

	switch (iMsg)
	{
	case WM_KEYDOWN:
		printf("key pressed\n");
		break;

	case WM_INITDIALOG:
		{
			char gimmeFolder[CRYPTIC_MAX_PATH];

			U32 iCurTime = timeSecondsSince2000_ForceRecalc();
			int iCurBranch;
			int iCurCoreBranch;
			HWND hOutPut;


			sprintf(gimmeFolder, "c:\\%s", GetProductName());
			iCurBranch = Gimme_GetBranchNum(gimmeFolder);

			iCurCoreBranch = Gimme_GetBranchNum("c:\\core");


			SetTextFast(GetDlgItem(hDlg, IDC_BRANCH), iCurBranch != -1 ? STACK_SPRINTF("%d", iCurBranch) : "!ERROR!");
			SetTextFast(GetDlgItem(hDlg, IDC_BRANCH_CORE), iCurCoreBranch != -1 ? STACK_SPRINTF("%d", iCurCoreBranch) : "!ERROR!");


			SetTextFast(GetDlgItem(hDlg, IDC_FROMSTRING), timeGetLocalGimmeStringFromSecondsSince2000(iCurTime - 60 * 60));
			SetTextFast(GetDlgItem(hDlg, IDC_TOSTRING), timeGetLocalGimmeStringFromSecondsSince2000(iCurTime));

			sprintf(tempStr, "%u", iCurTime - 60 * 60);
			SetTextFast(GetDlgItem(hDlg, IDC_FROMSECS), tempStr);


			sprintf(tempStr, "%u", iCurTime);
			SetTextFast(GetDlgItem(hDlg, IDC_TOSECS), tempStr);

			hOutPut = GetDlgItem(hDlg, IDC_OUTPUT);
			orig_OutputProc =  (WNDPROC)(LONG_PTR)SetWindowLongPtr(hOutPut,
				GWLP_WNDPROC, (LONG_PTR)MakeCtrlAWorkProc);

			
		}
		break;






	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case ID_GETFROMSTRINGS:
		case ID_GETFROMSECS2:
			{
				static char *pFromString = NULL;
				static char *pToString = NULL;
				static char *pBranchString = NULL;
				static char *pCoreBranchString = NULL;
				U32 iFromTime, iToTime;
				char *pOutString = NULL;
				int iBranch, iCoreBranch;
				U32 eFlags = GIMMEGETCHECKINS_FLAG_NO_CHECKINS_FROM_CBS | GIMMEGETCHECKINS_FLAG_NO_BLANK_COMMENTS;
				
				bool bPatchNotes = IsDlgButtonChecked(hDlg, IDC_PATCHNOTES);
				bool bShowBlanks = IsDlgButtonChecked(hDlg, IDC_SHOWBLANKS);
				bool bShowBuilders = IsDlgButtonChecked(hDlg, IDC_SHOWBUILDERS);
				bool bOutputCSV = IsDlgButtonChecked(hDlg, IDC_CSV);
				bool bRevNums = IsDlgButtonChecked(hDlg, IDC_REVNUMS);

				if(bShowBlanks)
				{
					eFlags &= ~GIMMEGETCHECKINS_FLAG_NO_BLANK_COMMENTS;
				}
				if(bShowBuilders)
				{
					eFlags &= ~GIMMEGETCHECKINS_FLAG_NO_CHECKINS_FROM_CBS;
				}

				estrStackCreate(&pOutString);

				if (LOWORD(wParam) == ID_GETFROMSTRINGS)
				{
				
					GetWindowText_UTF8(GetDlgItem(hDlg, IDC_FROMSTRING), &pFromString);
					GetWindowText_UTF8(GetDlgItem(hDlg, IDC_TOSTRING), &pToString);
					GetWindowText_UTF8(GetDlgItem(hDlg, IDC_BRANCH), &pBranchString);
					GetWindowText_UTF8(GetDlgItem(hDlg, IDC_BRANCH_CORE), &pCoreBranchString);

					iBranch = atoi(pBranchString);
					iCoreBranch = atoi(pCoreBranchString);

					iFromTime = timeGetSecondsSince2000FromLocalGimmeString(pFromString);
					iToTime = timeGetSecondsSince2000FromLocalGimmeString(pToString);

					if (iBranch < 0)
					{
						estrConcatf(&pOutString, "ERROR - couldn't parse %s. Must be a non-negative int", pBranchString);
					}
					if (iCoreBranch < 0)
					{
						estrConcatf(&pOutString, "ERROR - couldn't parse %s. Must be a non-negative int", pCoreBranchString);
					}


					if (iFromTime == 0)
					{
						estrConcatf(&pOutString, "ERROR - couldn't parse %s. Should be in format MMDDYYHH{:MM{:SS}}\n", pFromString);
					}

					if (iToTime == 0)
					{
						estrConcatf(&pOutString, "ERROR - couldn't parse %s. Should be in format MMDDYYHH{:MM{:SS}}\n", pToString);
					}

				}
				else
				{
					GetWindowText_UTF8(GetDlgItem(hDlg, IDC_FROMSECS), &pFromString);
					GetWindowText_UTF8(GetDlgItem(hDlg, IDC_TOSECS), &pToString);
					GetWindowText_UTF8(GetDlgItem(hDlg, IDC_BRANCH), &pBranchString);
					GetWindowText_UTF8(GetDlgItem(hDlg, IDC_BRANCH_CORE), &pCoreBranchString);

					iBranch = atoi(pBranchString);
					iCoreBranch = atoi(pCoreBranchString);

					if (!StringToUint(pFromString, &iFromTime))
					{
						estrConcatf(&pOutString, "ERROR - couldn't parse %s. Must be secs since 2000", pFromString);
						iFromTime = 0;
					}

					if (!StringToUint(pToString, &iToTime))
					{
						estrConcatf(&pOutString, "ERROR - couldn't parse %s. Must be secs since 2000", pToString);
						iToTime = 0;
					}
				}

				if (iFromTime > iToTime)
				{
					U32 temp = iFromTime;
					iFromTime = iToTime;
					iToTime = temp;
				}


				if (iFromTime && iToTime && iBranch >= 0)
				{
					CheckinInfo **ppCheckins = NULL;
					char *pFolderString = NULL;
					bool bResult;

					sprintf(tempStr, "%u", iFromTime);
					SetTextFast(GetDlgItem(hDlg, IDC_FROMSECS), tempStr);

					sprintf(tempStr, "%u", iToTime);
					SetTextFast(GetDlgItem(hDlg, IDC_TOSECS), tempStr);

					SetTextFast(GetDlgItem(hDlg, IDC_FROMSTRING), timeGetLocalGimmeStringFromSecondsSince2000(iFromTime));
					SetTextFast(GetDlgItem(hDlg, IDC_TOSTRING), timeGetLocalGimmeStringFromSecondsSince2000(iToTime));



					estrStackCreate(&pFolderString);
					
					if (stricmp(GetProductName(), "core") == 0)
					{
						estrPrintf(&pFolderString, "c:/core");
					}
					else
					{
						estrPrintf(&pFolderString, "c:/%s;c:/core", GetProductName());
					}

					
					bResult =  Gimme_GetCheckinsBetweenTimes_ForceBranch(iFromTime, iToTime, pFolderString, NULL, eFlags, &ppCheckins, 120, iBranch, iCoreBranch);

					if (!bResult)
					{
						estrPrintf(&pOutString, "ERROR: Gimme_GetCheckins failed.\r\n"
							"Please check:\r\n"
							"  1. Make sure that c:\\night\\tools\\bin is in your default path and is up to date.");
					}
					else
					{
						if (eaSize(&ppCheckins) == 0)
						{
							estrPrintf(&pOutString, "(No checkins)");
						}
						else
						{
							int i;

							for (i=0; i < eaSize(&ppCheckins); i++)
							{
								if (!bPatchNotes || strstri_safe(ppCheckins[i]->checkinComment, "PATCHNOTES:"))
								{
									if(bOutputCSV)
									{
										static char *estr = NULL;
										static char *pPatchNotes = NULL;
										char *pFound;
										estrClear(&pPatchNotes);
										estrCopy2(&estr, ppCheckins[i]->checkinComment);
										estrReplaceOccurrences(&estr, "\"", "\"\"");
										estrTrimLeadingAndTrailingWhitespace(&estr);

										/*if the string is of the form COMMENTS: foo PATCHNOTES: bar, chop it up and put
										  the comments and patchnotes in separate CSV fields*/
										if ((pFound = strstri(estr, "PATCHNOTES:")))
										{
											estrCopy2(&pPatchNotes, pFound + 11);
											estrTrimLeadingAndTrailingWhitespace(&pPatchNotes);
											estrSetSize(&estr, pFound - estr);
											if (strStartsWith(estr, "COMMENTS:"))
											{
												estrRemove(&estr, 0, 9);
											}
											estrTrimLeadingAndTrailingWhitespace(&estr);
										}

										estrConcatf(&pOutString, "%d,%s,%s,\"%s\",\"%s\",\r\n",
											ppCheckins[i]->iRevNum,
											ppCheckins[i]->userName,
											timeGetDateStringFromSecondsSince2000(ppCheckins[i]->iCheckinTimeSS2000),
											estr,
											pPatchNotes ? pPatchNotes : "");
									}
									else
									{
										if (bRevNums)
										{
											estrConcatf(&pOutString, "%s\tRev:%d(%s)\t%s\r\n",
												ppCheckins[i]->userName, ppCheckins[i]->iRevNum, timeGetLocalGimmeStringFromSecondsSince2000(ppCheckins[i]->iCheckinTimeSS2000), ppCheckins[i]->checkinComment);				
										}
										else
										{
											estrConcatf(&pOutString, "%s\t%s\t%s\r\n",
												ppCheckins[i]->userName, timeGetLocalGimmeStringFromSecondsSince2000(ppCheckins[i]->iCheckinTimeSS2000), ppCheckins[i]->checkinComment);
										}
									}
								}
							}
						}
					}


					estrDestroy(&pFolderString);
					eaDestroyEx(&ppCheckins, NULL);
				}

				SetTextFast(GetDlgItem(hDlg, IDC_OUTPUT), pOutString);

				estrDestroy(&pOutString);
			}
			break;

	
		}
		break;

		case WM_CLOSE:
			pWindow->bCloseRequested = true;

			return FALSE;

	}
	
	return FALSE;
}
