// CrypticTorrent.cpp : Defines the entry point for the application.

#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include "CrypticTorrent.h"
#include "DeepSpace.hpp"

#define MAX_PROJECTNAME_LENGTH 255
static char sProjectName[MAX_PROJECTNAME_LENGTH+1] = "";

#define MAX_INSTALLERNAME_LENGTH 255
static char sInstallerName[MAX_INSTALLERNAME_LENGTH+1] = "";

// define this to not rely on the torrent file itself to create the subdir ("StarTrekSetupFiles")
// #define AUTO_PREPEND_SUBDIR

#pragma warning(disable: 4267 4244 4535)

#include <boost/algorithm/string/predicate.hpp>

#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/session.hpp"

#define ARRAY_SIZE_CHECKED(n) (sizeof(n) / ((sizeof(n)==0 || sizeof(n)==4 || sizeof(n)==8)?0:sizeof((n)[0])))
#define SAFESTR(str) str, ARRAY_SIZE_CHECKED(str)

BOOL CheckAndInitResources(HINSTANCE hInstance);
BOOL AddEmbeddedTorrent(HINSTANCE hInstance, const char *sDownloadPath);
BOOL SelectFolder(char *path, size_t len);
INT_PTR CALLBACK TorrentDialogProc(HWND, UINT, WPARAM, LPARAM);

using namespace libtorrent;

// Period for calculating current download rate, in seconds.
const double fRatePeriod = 60*1;  // one minute

// Minimum download rate before falling back to HTTP seed, in bytes per second.
const double fMinimumRate = 187500;  // 1.5 mbit/s

// Multiple of uMinimumRate above which HTTP seeding should be turned off.
const double fRestartMultiple = 1.25;

// Download reporting interval.
const double fDownloadReportPeriod = 60*5;

static session sSession;
static torrent_info *sTorrentInfo;
static torrent_handle shTorrent;
bool sFastStart = false;						// True if we're currently in fast start
bool sFallback = false;							// True if we've switched to the fallback
bool sFallbackEver = false;						// True if we've ever switched to the fallback
static char sDownloadPath[MAX_PATH] = {0};
static char sStatusText[2048] = "Initializing...";
static HFONT shFont;
static HINSTANCE shInstance;

// Some statistics
bool sbStartedDownload = false;
bool sbGotBytes = false;
int siDownloadPercent = 0;
bool sbDownloadFinished = false;
bool sbInstallerRan = false;

// GetRight-style fallback HTTP URL seed.
static char sFallbackHttpSeed[] = "http://installers.crypticstudios.com/torrents/";

int APIENTRY WinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPTSTR    lpCmdLine,
                     int       nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	shInstance = hInstance;

	// Load configuration from resources.
	if(!CheckAndInitResources(hInstance))
		return 0;

	// Transmit initial report to Deep Space Network.
	DeepSpaceInitInfo(sProjectName, sTorrentInfo->info_hash().to_string().c_str());
	DeepSpaceReportStartup();

	// Get executable directory to suggest download path.
	GetModuleFileName(NULL, sDownloadPath, MAX_PATH);
	char *lastSlash = strrchr(sDownloadPath, '\\');
	if(lastSlash)
		*lastSlash = 0;

	// Ask user to confirm download path.
	if(!SelectFolder(SAFESTR(sDownloadPath)))
	{
		DeepSpaceSyncReportExit(sbStartedDownload, sbGotBytes, siDownloadPercent, sbDownloadFinished, sbInstallerRan);
		return 0;
	}

#if AUTO_PREPEND_SUBDIR
	strncat(sDownloadPath, "\\" CRYPTIC_PROJECT_NAME "SetupFiles", MAX_PATH);
	sDownloadPath[MAX_PATH-1] = 0;
#endif

	boost::filesystem::path::default_name_check(boost::filesystem::no_check);
	sSession.listen_on(std::make_pair(6881, 6889));
	AddEmbeddedTorrent(hInstance, sDownloadPath);

	InitCommonControls();
	DialogBox(hInstance, MAKEINTRESOURCE(IDD_TORRENT_DIALOG), NULL, TorrentDialogProc);

	DeepSpaceSyncReportExit(sbStartedDownload, sbGotBytes, siDownloadPercent, sbDownloadFinished, sbInstallerRan);

	return 0;
}

int CALLBACK SetSelProc (HWND hWnd,
						 UINT uMsg,
						 LPARAM lParam,
						 LPARAM lpData)
{
	if (uMsg==BFFM_INITIALIZED)
	{
		::SendMessage(hWnd, BFFM_SETSELECTION, TRUE, lpData );
	}
	return 0;
}

BOOL SelectFolder(char *path, size_t len)
{
	BROWSEINFO   bi; 
	ZeroMemory(&bi,   sizeof(bi)); 
	TCHAR   szDisplayName[MAX_PATH]; 
	szDisplayName[0]    =  0;  

	bi.hwndOwner      = NULL; 
	bi.pidlRoot       = NULL; 
	bi.pszDisplayName = szDisplayName; 
	bi.lpszTitle      = "Please select a folder to save temporary install files:"; 
	bi.ulFlags        = BIF_RETURNONLYFSDIRS;
    bi.lpfn           = SetSelProc;
    bi.lParam         = (LPARAM)path;
	bi.iImage         = 0;  

	LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
	TCHAR   szPathName[MAX_PATH]; 
	if(NULL != pidl)
	{
		BOOL bRet = SHGetPathFromIDList(pidl, szPathName);
		if(bRet)
		{
			strncpy_s(path, len, szPathName, MAX_PATH);
			path[MAX_PATH-1] = 0;
			return TRUE;
		}
	}

	return FALSE;
}

int GetResourceSize(HINSTANCE hInstance, const char *resName, const char *resType)
{
	HRSRC hRes = FindResource(hInstance, resName, resType);
	if (hRes == NULL)
	{
		return 0;
	}

	return SizeofResource(hInstance, hRes);
}

void * GrabResource(HINSTANCE hInstance, const char *resName, const char *resType, int *outputSize)
{
	HRSRC hRes = FindResource(hInstance, resName, resType);
	if (hRes == NULL)
	{
		return NULL;
	}

	unsigned int resSize = SizeofResource(hInstance, hRes);
	if(outputSize)
	{
		*outputSize = resSize;
	}

	HGLOBAL hResLoad = LoadResource(hInstance, hRes);
	if (hResLoad == NULL)
	{
		return NULL;
	}

	char *lpResLock = (char *)LockResource(hResLoad);
	if (lpResLock == NULL)
	{
		return NULL;
	}

	return lpResLock;
}

BOOL CheckAndInitResources(HINSTANCE hInstance)
{
	int projNameLen = 0;
	char *projName  = (char *)GrabResource(hInstance, "PROJECTNAME", "DATA", &projNameLen);

	if((!projName) 
	|| (projNameLen < 2) 
	|| (projNameLen > MAX_PROJECTNAME_LENGTH))
	{
		MessageBox(NULL, "Embedded project name not found.", "CrypticDownloader Error", MB_OK);
		return FALSE;
	}

	strncpy_s(SAFESTR(sProjectName), projName, projNameLen);
	sProjectName[projNameLen+1] = 0;

	// ---------------------------------------------------------------------------

	int instNameLen = 0;
	char *instName  = (char *)GrabResource(hInstance, "INSTALLERNAME", "DATA", &instNameLen);

	if((instName) 
	&& (instNameLen >= 2) 
	&& (instNameLen < MAX_INSTALLERNAME_LENGTH))
	{
		strncpy_s(SAFESTR(sInstallerName), instName, instNameLen);
		sInstallerName[instNameLen+1] = 0;
	}

	// ---------------------------------------------------------------------------

	int torrentSize;
	char *torrent = (char *)GrabResource(hInstance, "TORRENT", "DATA", &torrentSize);
	if(!torrent || torrentSize < 2)
	{
		MessageBox(NULL, "Embedded torrent not found.", "CrypticDownloader Error", MB_OK);
		return FALSE;
	}

	sTorrentInfo = new torrent_info(torrent, torrentSize);

	return TRUE;
}

BOOL AddEmbeddedTorrent(HINSTANCE hInstance, const char *sDownloadPath)
{
	add_torrent_params p;
	p.save_path = sDownloadPath;

	int torrentSize = 0;
	char *torrent = (char *)GrabResource(hInstance, "TORRENT", "DATA", &torrentSize);
	if(!torrent || torrentSize < 2)
		return FALSE;

	p.ti = sTorrentInfo;
	p.ti->add_url_seed(sFallbackHttpSeed);
	sFastStart = true;
	shTorrent = sSession.add_torrent(p);
	return TRUE;
}

// Switch to fallback seeding, if not already using it.
void StartFallback()
{
	if (!sFallback)
	{
		sFallback = true;
		shTorrent.add_url_seed(sFallbackHttpSeed);
	}
}

// Turn off fallback seeding, if it is on.
void StopFallback()
{
	if (sFallback || sFastStart)
	{
		sFallback = false;
		sFastStart = false;
		shTorrent.remove_url_seed(sFallbackHttpSeed);
	}
}

// Return true if this a web seeder.
bool isWebSeed(const peer_info &peer)
{
	static const char urlSeedPrefix[] = "URL seed @ ";
	return boost::starts_with(peer.client, urlSeedPrefix);
}

// Switch to fallback seeding if the download speed drops too low.
void CheckDownloadSpeed(const torrent_status &ts)
{
	// Initialize tracking if not initialized.
	static bool uLastPeriodInitialized = false;
	static time_t uLastPeriodStart = 0;
	static size_type uLastPeriodDownloaded = 0;
	if (!uLastPeriodInitialized)
	{
		uLastPeriodStart = time(0);
		uLastPeriodDownloaded = ts.total_payload_download;
		uLastPeriodInitialized = true;
	}

	// Check if the period has expired, and check the bandwidth for the last period if it has.
	double elapsed = difftime(time(0), uLastPeriodStart);
	if (elapsed >= fRatePeriod)
	{
		double rate = (ts.total_payload_download - uLastPeriodDownloaded) / fRatePeriod;

		// Check if we need to start the fallback.
		if (!sFallback && rate < fMinimumRate)
			StartFallback();

		// Check if we need to stop the fallback.
		if ((sFastStart || sFallback) && rate > fRestartMultiple * fMinimumRate)
		{
			// Get URL seed download rate.
			typedef std::vector<peer_info> peer_type;
			peer_type peers;
			shTorrent.get_peer_info(peers);
			double seedRate = 0;
			for (peer_type::const_iterator i = peers.begin(); i != peers.end(); ++i)
			{
				if (isWebSeed(*i))
				{
					seedRate = i->payload_down_speed;
					break;
				}
			}

			// If this client no longer seems to be dependent on the URL seed for download, turn off fallback mode.
			if (rate - seedRate > fRestartMultiple * fMinimumRate && ts.download_payload_rate - seedRate > fRestartMultiple * fMinimumRate)
				StopFallback();
		}

		// Record period status.
		uLastPeriodStart = time(0);
		uLastPeriodDownloaded = ts.total_payload_download;
	}
}

// Periodically report our progress to the Deep Space Network..
void ReportPeriodic(const torrent_status &ts, bool force)
{
	static time_t last = 0;
	time_t now = time(0);

	// Run every fDownloadReportPeriod.
	double elapsed = difftime(now, last);
	if (elapsed > fDownloadReportPeriod || force)
	{
		last = now;

		unsigned __int64 total_web_download = 0;
		unsigned __int64 total_cryptic_download = 0;

		typedef std::vector<peer_info> peer_type;
		peer_type peers;
		shTorrent.get_peer_info(peers);
		for (peer_type::const_iterator i = peers.begin(); i != peers.end(); ++i)
		{
			// Get the total amount downloaded from web.
			if (isWebSeed(*i))
			{
				total_web_download += i->total_download;
				continue;
			}

			// Get the total amount downloaded from Cryptic seeds.
			if (i->ip.address().is_v4())
			{
				unsigned long addr = i->ip.address().to_v4().to_ulong();
				unsigned long mask21 = addr & 0xfffff800;
				if (mask21 == 0xd05fb800)  // Check if it's in 208.95.184.0/21 (CRYPTIC-NET-PWNAGE-US01)
					total_cryptic_download += i->total_download;
			}
		}

		// Copy blob.
		struct DeepSpacePeriodicTorrentBlob blob;
		blob.paused = ts.paused;
		blob.progress = ts.progress;
		blob.total_download = ts.total_download;
		blob.total_upload = ts.total_upload;
		blob.total_payload_download = ts.total_payload_download;
		blob.total_payload_upload = ts.total_payload_upload;
		blob.total_failed_bytes = ts.total_failed_bytes;
		blob.total_redundant_bytes = ts.total_redundant_bytes;
		blob.download_rate = ts.download_rate;
		blob.upload_rate = ts.upload_rate;
		blob.download_payload_rate = ts.download_payload_rate;
		blob.upload_payload_rate = ts.upload_payload_rate;
		blob.num_seeds = ts.num_seeds;
		blob.num_peers = ts.num_peers;
		blob.num_complete = ts.num_complete;
		blob.num_incomplete = ts.num_incomplete;
		blob.list_seeds = ts.list_seeds;
		blob.list_peers = ts.list_peers;
		blob.connect_candidates = ts.connect_candidates;
		blob.num_pieces = ts.num_pieces;
		blob.total_done = ts.total_done;
		blob.total_wanted_done = ts.total_wanted_done;
		blob.total_wanted = ts.total_wanted;
		blob.distributed_copies = ts.distributed_copies;
		blob.block_size = ts.block_size;
		blob.num_uploads = ts.num_uploads;
		blob.num_connections = ts.num_connections;
		blob.uploads_limit = ts.uploads_limit;
		blob.connections_limit = ts.connections_limit;
		blob.all_time_upload = ts.all_time_upload;
		blob.all_time_download = ts.all_time_download;
		blob.active_time = ts.active_time;
		blob.seeding_time = ts.seeding_time;
		blob.seed_rank = ts.seed_rank;
		blob.last_scrape = ts.last_scrape;
		blob.has_incoming = ts.has_incoming;

		blob.using_fallback = sFallback;
		blob.ever_used_fallback = sFallbackEver;
		blob.total_web_download = total_web_download;
		blob.total_cryptic_download = total_cryptic_download;

		// Send blob.
		DeepSpaceSyncReportPeriodicDownload(force, blob);
	}
}

INT_PTR CALLBACK TorrentDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	static int sComplete = 0;
	static HBITMAP shBitmap = (HBITMAP)INVALID_HANDLE_VALUE;

	switch (message)
	{
	case WM_INITDIALOG:
		SendMessage(GetDlgItem(hDlg, IDC_PROGRESS), PBM_SETRANGE, 0, MAKELPARAM(0, 100)); 
		SetTimer(hDlg, 5, 1000, NULL);
		shBitmap = LoadBitmap(shInstance, "BACKGROUND");
		shFont = CreateFont(12,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH,TEXT("Tahoma"));

		SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)LoadIcon(shInstance, MAKEINTRESOURCE(IDI_CRYPTICTORRENT)));
		SendMessage(hDlg, WM_SETICON, ICON_BIG,   (LPARAM)LoadIcon(shInstance, MAKEINTRESOURCE(IDI_CRYPTICTORRENT)));
		return (INT_PTR)TRUE;

	case WM_ERASEBKGND:
		{
			HDC dc = GetDC(hDlg);
			HDC bmpdc = CreateCompatibleDC(dc);
			HGDIOBJ oldbmp = SelectObject(bmpdc, shBitmap);
			RECT r;
			GetClientRect(hDlg, &r);

			BitBlt(dc, 0, 0, r.right, r.bottom, bmpdc, 0, 25, SRCCOPY);
			SelectObject(bmpdc, oldbmp);
			DeleteDC(bmpdc);

			SelectObject(dc, shFont);
			SetBkMode(dc, TRANSPARENT);

			r.left += 10;
			r.top  += 6;
			SetTextColor(dc, RGB(0,0,0));
			DrawText(dc, sStatusText, strlen(sStatusText), &r, DT_TOP|DT_LEFT|DT_SINGLELINE);

			r.left -= 1;
			r.top  -= 1;
			SetTextColor(dc, RGB(255,255,255));
			DrawText(dc, sStatusText, strlen(sStatusText), &r, DT_TOP|DT_LEFT|DT_SINGLELINE);

			ReleaseDC(hDlg, dc);
			return (INT_PTR)TRUE;
		}

	case WM_PAINT:
			return (INT_PTR)FALSE;

	case WM_TIMER:
		{
			torrent_status ts = shTorrent.status();
			int perc = (int)(ts.progress * 100.0f);

			if(ts.state == torrent_status::finished || ts.state == torrent_status::seeding)
			{
				if(!sComplete)
				{
					char tempStr[512];
					SendMessage(GetDlgItem(hDlg, IDC_PROGRESS), PBM_SETPOS, 100, 0);
					SetWindowText(GetDlgItem(hDlg, IDC_ACTION), "Install");
					sprintf_s(SAFESTR(tempStr), "[Done] %s Downloader", sProjectName);
					SetWindowText(hDlg, tempStr);
					strcpy_s(SAFESTR(sStatusText), "Download Complete. Please press Install.");
					InvalidateRect(hDlg, NULL, TRUE);
					sComplete = 1;

					sbStartedDownload = true;
					sbGotBytes = true;
					siDownloadPercent = 100;
					sbDownloadFinished = true;

					ReportPeriodic(ts, true);
				}
			}
			else
			{
				char tempStr[512];
				float fPercentage = ts.progress * 100.0f;
				sprintf_s(SAFESTR(tempStr), "[%d%%] %s Downloader", (int)fPercentage, sProjectName);
				SetWindowText(hDlg, tempStr);

				if(ts.state == torrent_status::downloading)
				{
					// Check download speed on some period.
					CheckDownloadSpeed(ts);

					sprintf_s(sStatusText, sizeof(sStatusText), "Downloading... (%2.2f%%, %I64u/%I64uMB, %2.2fKB/s%s)", 
						fPercentage,
						ts.total_wanted_done / (1024 * 1024), ts.total_wanted / (1024 * 1024),
						ts.download_rate / 1024.0f,
						(sFallback ? " H" : ""));

					sbStartedDownload = true;
					if (ts.total_wanted_done)
						sbGotBytes = true;
					siDownloadPercent = fPercentage;

					ReportPeriodic(ts, false);
				}
				else
				{
					sprintf_s(sStatusText, sizeof(sStatusText), "Processing... (%2.2f%%, %I64u/%I64uMB)", 
						fPercentage,
						ts.total_wanted_done / (1024 * 1024), ts.total_wanted / (1024 * 1024));
				}

				SendMessage(GetDlgItem(hDlg, IDC_PROGRESS), PBM_SETPOS, perc, 0);
				InvalidateRect(hDlg, NULL, TRUE);
			}
			return (INT_PTR)TRUE;
		}

	case WM_COMMAND:
		if (LOWORD(wParam) == IDCANCEL)
		{
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		else if(LOWORD(wParam) == IDC_ACTION)
		{
			if(sComplete)
			{
				char setupPath[MAX_PATH];
				strncpy_s(SAFESTR(setupPath), sDownloadPath, MAX_PATH);
				setupPath[MAX_PATH-1] = 0;

				if(strlen(sInstallerName) == 0)
				{
#if !AUTO_PREPEND_SUBDIR
					// We need to assume the torrent used this folder name inside of it, and add it here
					strncat_s(SAFESTR(setupPath), "\\", MAX_PATH);
					setupPath[MAX_PATH-1] = 0;

					std::string name = shTorrent.name();
					if(name.length())
					{
						strncat_s(SAFESTR(setupPath), name.c_str(), MAX_PATH);
						setupPath[MAX_PATH-1] = 0;
					}
					else
					{
						strncat_s(SAFESTR(setupPath), sProjectName, MAX_PATH);
						setupPath[MAX_PATH-1] = 0;
						strncat_s(SAFESTR(setupPath), "SetupFiles", MAX_PATH);
						setupPath[MAX_PATH-1] = 0;
					}
#endif
					strncat_s(SAFESTR(setupPath), "\\setup.exe", MAX_PATH);
					setupPath[MAX_PATH-1] = 0;
				}
				else
				{
					// Use built-in installer name
					strncat_s(SAFESTR(setupPath), "\\", MAX_PATH);
					setupPath[MAX_PATH-1] = 0;
					strncat_s(SAFESTR(setupPath), sInstallerName, MAX_PATH);
					setupPath[MAX_PATH-1] = 0;
				}

				sbInstallerRan = true;

				ShellExecute(NULL, NULL, setupPath, NULL, NULL, SW_SHOW);
			}
			EndDialog(hDlg, IDCANCEL);
		}
		break;
	}
	return (INT_PTR)FALSE;
}
