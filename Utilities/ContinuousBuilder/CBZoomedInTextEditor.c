#include "CBZoomedInTextEditor.h"
#include "earray.h"
#include "textparser.h"
#include "file.h"
#include "WinInclude.h"
#include "Estring.h"
#include "WinUtil.h"
#include "resource.h"
#include "ContinuousBuilder.h"
#include "qsortG.h"

static char **sppInOutText = NULL;
static char *spComment = NULL;
static enumZiteType seZiteType;

int SortStringsByValidIntness(const char **ppStr1, const char **ppStr2)
{
	U32 iVal1;
	bool bIsInt1;
	U32 iVal2;
	bool bIsInt2;

	bIsInt1 = StringToUint_Paranoid(*ppStr1, &iVal1);
	bIsInt2 = StringToUint_Paranoid(*ppStr2, &iVal2);

	if (!bIsInt1 && !bIsInt2)
	{
		return stricmp(*ppStr1, *ppStr2);
	}

	if (!bIsInt1)
	{
		return -1;
	}

	if (!bIsInt2)
	{
		return 1;
	}

	if (iVal1 < iVal2)
	{
		return -1;
	}

	if (iVal1 > iVal2)
	{
		return 1;
	}

	return 0;
}
bool HasValidBadIntPrefix(const char *pStr)
{
	if (strStartsWith(pStr, "DUPLICATE_INT:"))
	{
		return true;
	}
	if (strStartsWith(pStr, "INVALID_INT:"))
	{
		return true;
	}

	return false;
}


BOOL CALLBACK ziteDlgProc (HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{


	switch (iMsg)
	{

	case WM_INITDIALOG:

		SetTextFast(GetDlgItem(hDlg, IDC_COMMENT), spComment);


		switch (seZiteType)
		{
			xcase ZITE_SORTED_INTS:
			case ZITE_COMMA_SEPARATED:
			{
				char **ppSubStrings = NULL;
				char *pFullString = NULL;
				int i;

				DivideString(*sppInOutText, ",\r\n ", &ppSubStrings, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);

				for (i=0; i < eaSize(&ppSubStrings); i++)
				{
					estrConcatf(&pFullString, "%s%s", i == 0 ? "" : "\r\n", ppSubStrings[i]);
				}
				SetTextFast(GetDlgItem(hDlg, IDC_EDIT_TEXT), pFullString);
				ShowWindow(GetDlgItem(hDlg, IDC_CHECK_SORT), SW_SHOW);

				estrDestroy(&pFullString);
				eaDestroyEx(&ppSubStrings, NULL);
			}


			xdefault:
			{
				SetTextFast(GetDlgItem(hDlg, IDC_EDIT_TEXT), *sppInOutText);
				ShowWindow(GetDlgItem(hDlg, IDC_CHECK_SORT), SW_HIDE);
			}
		}

		return true; 

	

	case WM_CLOSE:
		EndDialog(hDlg, 0);

		break;

	case WM_COMMAND:
		switch (LOWORD (wParam))
		{
		case IDCANCEL:
			EndDialog(hDlg, 0);
			break;

		case IDOK:
			{
				switch (seZiteType)
				{
					xcase ZITE_SORTED_INTS:
					case ZITE_COMMA_SEPARATED:
					{
						char **ppSubStrings = NULL;
						char *pCurText = NULL;
						int i;

						estrClear(sppInOutText);
						estrGetWindowText(&pCurText, GetDlgItem(hDlg, IDC_EDIT_TEXT));
						DivideString(pCurText, ",\r\n ", &ppSubStrings, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);

						for (i=0; i < eaSize(&ppSubStrings); i++)
						{
							estrConcatf(sppInOutText, "%s%s", i == 0 ? "" : ", ", ppSubStrings[i]);
						}

						estrDestroy(&pCurText);
						eaDestroyEx(&ppSubStrings, NULL);


					}

					xdefault:
					{
						estrGetWindowText(sppInOutText, GetDlgItem(hDlg, IDC_EDIT_TEXT));
					}
				}
					
				EndDialog(hDlg, 0);

			}
			break;

		case IDC_CHECK_SORT:
			{
			switch (seZiteType)
				{
					xcase ZITE_SORTED_INTS:
					{
					
						char **ppSubStrings = NULL;
						char *pCurText = NULL;
						int i;

						estrGetWindowText(&pCurText, GetDlgItem(hDlg, IDC_EDIT_TEXT));
						DivideString(pCurText, ",\r\n ", &ppSubStrings, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);

						eaQSort(ppSubStrings, SortStringsByValidIntness);

						for (i=0; i < eaSize(&ppSubStrings); i++)
						{
							U32 iVal;
							if (StringToUint_Paranoid(ppSubStrings[i], &iVal))
							{
								if (i < eaSize(&ppSubStrings) - 1)
								{
									if (stricmp(ppSubStrings[i], ppSubStrings[i+1]) == 0)
									{
										static char *pTemp = NULL;
										estrPrintf(&pTemp, "DUPLICATE_INT:%s", ppSubStrings[i]);
										SAFE_FREE(ppSubStrings[i]);
										ppSubStrings[i] = strdup(pTemp);
									}
								}
							}
							else
							{
								if (!HasValidBadIntPrefix(ppSubStrings[i]))
								{
									static char *pTemp = NULL;
									estrPrintf(&pTemp, "INVALID_INT:%s", ppSubStrings[i]);
									SAFE_FREE(ppSubStrings[i]);
									ppSubStrings[i] = strdup(pTemp);
								}
							}
						}

						eaQSort(ppSubStrings, SortStringsByValidIntness);

						estrClear(&pCurText);
						for (i=0; i < eaSize(&ppSubStrings); i++)
						{
							estrConcatf(&pCurText, "%s%s", i == 0 ? "" : "\r\n", ppSubStrings[i]);
						}

						SetTextFast(GetDlgItem(hDlg, IDC_EDIT_TEXT), pCurText);

						estrDestroy(&pCurText);
						eaDestroyEx(&ppSubStrings, NULL);
					}


				xcase ZITE_COMMA_SEPARATED:
					{
						char **ppSubStrings = NULL;
						char *pCurText = NULL;
						int i;

						estrGetWindowText(&pCurText, GetDlgItem(hDlg, IDC_EDIT_TEXT));
						DivideString(pCurText, ",\r\n ", &ppSubStrings, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);

						eaQSort(ppSubStrings, strCmp);

						estrClear(&pCurText);
						for (i=0; i < eaSize(&ppSubStrings); i++)
						{
							estrConcatf(&pCurText, "%s%s", i == 0 ? "" : "\r\n", ppSubStrings[i]);
						}

						SetTextFast(GetDlgItem(hDlg, IDC_EDIT_TEXT), pCurText);

						estrDestroy(&pCurText);
						eaDestroyEx(&ppSubStrings, NULL);
					}
				}


			}
			break;
		
		}
		break;


	}

	return false;
}

void InvokeZoomedInTextEditor(char *pComment, char **ppInOutString, enumZiteType eType)
{
	sppInOutText = ppInOutString;
	spComment = pComment;
	seZiteType = eType;

	DialogBox(winGetHInstance(), MAKEINTRESOURCE(IDD_ZITE), ghCBDlg, (DLGPROC)ziteDlgProc);
}