﻿#include <HTTPRequest.hpp>
#include <iostream>
#include <Windows.h>
#include <string>
#include <sstream>
#include <discord-sdk/discord.h>
#include <codecvt>
#include <shellapi.h>
#include "resource.h"
#include <strsafe.h>

constexpr int64_t DS_CLIENT_ID = 904704369998561321;
constexpr wchar_t MSN_CLASS_NAME[] = L"MsnMsgrUIManager";
constexpr unsigned int IDT_TIMER_DISCORD_TICK = (WM_USER + 1);
constexpr unsigned int NOTIFICATION_TRAY_ICON = (WM_USER + 2);

HINSTANCE hInstance;

discord::Core* core{};

void ReplaceAll(std::string& string, const std::string& search, const std::string& replace) {
	size_t pos = string.find(search);
	while (pos != std::string::npos) {
		string.replace(pos, search.size(), replace);
		pos = string.find(search, pos+replace.size());
	}
}

std::string GetAlbumImageURL(const std::string& artist, const std::string& album) {
	std::string requestURL = "http://api.deezer.com/search/album?q=artist:\"" + artist + "\" album:\"" + album + "\"";
	ReplaceAll(requestURL, " ", "%20");
	http::Request request(requestURL);
	const auto response = request.send("GET");
	std::string coverUrl{ response.body.begin(), response.body.end() };

	if (response.status != http::Response::Status::Ok) {
		std::cerr << coverUrl << std::endl;
	}

	int index = coverUrl.find("cover") + 8;
	coverUrl = coverUrl.substr(index);
	index = coverUrl.find("\"");
	coverUrl = coverUrl.substr(0, index);

	ReplaceAll(coverUrl, "\\", "");
	return coverUrl;
}

void SetDiscordPlaying(const std::string& title, const std::string& album, const std::string& artist) {
	std::string details = artist + " - " + title;
	std::string albumCover = GetAlbumImageURL(artist, album);

	discord::Activity activity{};
	activity.SetDetails(details.c_str());
	activity.SetState(album.c_str());
	activity.GetAssets().SetSmallImage("original_zune_logo");
	activity.GetAssets().SetLargeImage(albumCover.c_str());

	core->ActivityManager().UpdateActivity(activity, [](discord::Result result) {
		if (result != discord::Result::Ok) {
			std::cerr << "Failed to update Discord Activity" << std::endl;
		}
	});
}

void SetDiscordStopped() {
	core->ActivityManager().ClearActivity([](discord::Result result) {
		if (result != discord::Result::Ok) {
			std::cerr << "Failed to update Discord Activity" << std::endl;
		}
	});
}


void OnCopyData(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	COPYDATASTRUCT* data = (COPYDATASTRUCT*)lParam;
	std::wstring wideZuneMsg((wchar_t*)data->lpData, (size_t)data->cbData / 2);
	std::string zuneMsg;

	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> conv;
	zuneMsg = conv.to_bytes(wideZuneMsg);

	//Only look for messages comming from Zune
	if (zuneMsg.substr(0, 4) != "ZUNE") {
		return;
	}

	if (zuneMsg.length() == 22) {
		//Not Playing
		std::cout << "Not Playing" << std::endl;
		SetDiscordStopped();
	}
	else {
		//Playing
		std::stringstream tmp(zuneMsg);
		std::string segment;
		int index = 0;

		std::string artist;
		std::string title;
		std::string album;

		while (std::getline(tmp, segment, '\\')) {
			if (index == 4) {
				title = segment.erase(0, 1);
			}
			else if (index == 5) {
				artist = segment.erase(0, 1);
			}
			else if (index == 6) {
				album = segment.erase(0, 1);
			}

			index++;
		}


		std::cout << "Artist: " << artist << std::endl;
		std::cout << "Title: " << title << std::endl;
		std::cout << "Album: " << album << std::endl;
		SetDiscordPlaying(title, album, artist);
	}
}

void OnTimer() {
	core->RunCallbacks();
}

LRESULT OnCommand(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (LOWORD(wParam))
	{
	case ID_ZUNEDISCORDRPC_SHOWCONSOLE: {
		HWND hConsole = GetConsoleWindow();
		int show = IsWindowVisible(hConsole) == true ? SW_HIDE : SW_SHOW;
		ShowWindow(GetConsoleWindow(), show);
		}
		break;
	case ID_ZUNEDISCORDRPC_EXIT:
		DestroyWindow(hWnd);
		break;
	default:
		return DefWindowProc(hWnd, uMsg, wParam, lParam);
		break;
	}

	return 0;
}

void OnTrayIcon(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	if (LOWORD(lParam) == WM_CONTEXTMENU) {
		HMENU hMenu = LoadMenu(hInstance, MAKEINTRESOURCE(IDR_CONTEXT_MENU));
		HMENU hSubMenu = GetSubMenu(hMenu, 0);

		SetForegroundWindow(hWnd);

		POINT pt = { LOWORD(wParam), HIWORD(wParam) };

		UINT uFlags = TPM_RIGHTBUTTON;
		if (GetSystemMetrics(SM_MENUDROPALIGNMENT) != 0) {
			uFlags |= TPM_RIGHTALIGN;
		}
		else {
			uFlags |= TPM_LEFTALIGN;
		}

		TrackPopupMenuEx(hSubMenu, uFlags, pt.x, pt.y, hWnd, NULL);
		DestroyMenu(hSubMenu);
	}
}

void OnDestroy(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	KillTimer(hWnd, IDT_TIMER_DISCORD_TICK);
	NOTIFYICONDATA trayIcon{};
	trayIcon.cbSize = sizeof(trayIcon);
	trayIcon.hWnd = hWnd;
	trayIcon.uID = IDI_ICON;
	Shell_NotifyIcon(NIM_DELETE, &trayIcon);
	PostQuitMessage(0);
}


LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
		case WM_COPYDATA: OnCopyData(hWnd, uMsg, wParam, lParam); break;
		case WM_TIMER: OnTimer(); break;
		case WM_COMMAND: return OnCommand(hWnd, uMsg, wParam, lParam); break;
		case NOTIFICATION_TRAY_ICON: OnTrayIcon(hWnd, uMsg, wParam, lParam); break;
		case WM_DESTROY: OnDestroy(hWnd, uMsg, wParam, lParam); break;
		default: return DefWindowProc(hWnd, uMsg, wParam, lParam); break;
	}

	return 0;
}

int main() {
	hInstance = GetModuleHandle(NULL);
	//Create Hidden Window to receive messges from Zune
	WNDCLASS wc = {};
	wc.lpszClassName = MSN_CLASS_NAME;
	wc.hInstance = hInstance;
	wc.lpfnWndProc = WindowProc;
	wc.lpszMenuName = MAKEINTRESOURCE(IDR_CONTEXT_MENU);
	RegisterClass(&wc);

	HWND hWnd = CreateWindow(MSN_CLASS_NAME, L"", WS_DISABLED, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, hInstance, NULL);
	if (!hWnd) {
		std::cerr << "Error: Could not create window!" << std::endl;
		return -1;
	}
	std::cout << "Created MSN Fake Window!" << std::endl;

	//Init Discord
	auto result = discord::Core::Create(DS_CLIENT_ID, DiscordCreateFlags_Default, &core);
	if (result != discord::Result::Ok) {
		std::cerr << "Could not initialize discord rpc" << std::endl;
		return -1;
	}
	std::cout << "Initialized Discord RPC" << std::endl;

	//Set Tick timer for discord
	SetTimer(hWnd, IDT_TIMER_DISCORD_TICK, 500, NULL);

	HMENU menue = CreatePopupMenu();
	AppendMenu(menue, MF_STRING, 0, L"Show Console");
	AppendMenu(menue, MF_STRING, 0, L"Exit");
	
	//Show Tray Icon
	NOTIFYICONDATA trayIcon{};
	trayIcon.cbSize = sizeof(trayIcon);
	trayIcon.uVersion = NOTIFYICON_VERSION_4;
	trayIcon.hWnd = hWnd;
	trayIcon.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
	trayIcon.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON));
	trayIcon.uID = IDI_ICON;
	trayIcon.uCallbackMessage = NOTIFICATION_TRAY_ICON;
	StringCchCopy(trayIcon.szTip, ARRAYSIZE(trayIcon.szTip), L"ZuneDiscordRPC");
	Shell_NotifyIcon(NIM_ADD, &trayIcon);
	Shell_NotifyIcon(NIM_SETVERSION, &trayIcon);


	ShowWindow(GetConsoleWindow(), SW_HIDE);

	//Start msg pump
	UpdateWindow(hWnd);
	MSG msg = {};
	while (GetMessage(&msg, NULL, 0, 0) > 0) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	std::cout << "Shutting down..." << std::endl;

	return 0;
}
