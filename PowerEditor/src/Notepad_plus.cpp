//this file is part of notepad++
//Copyright (C)2003 Don HO ( donho@altern.org )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
//
//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include "precompiledHeaders.h"
#include "Notepad_plus.h"
#include "FileDialog.h"
#include "printer.h"
#include "FileNameStringSplitter.h"
#include "lesDlgs.h"
#include "Utf8_16.h"
#include "regExtDlg.h"
#include "RunDlg.h"
#include "ShortcutMapper.h"
#include "preferenceDlg.h"
#include "TaskListDlg.h"
#include "xmlMatchedTagsHighlighter.h"
#include "EncodingMapper.h"

const TCHAR Notepad_plus::_className[32] = TEXT("Notepad++");
HWND Notepad_plus::gNppHWND = NULL;


enum tb_stat {tb_saved, tb_unsaved, tb_ro};

#define DIR_LEFT true
#define DIR_RIGHT false


Notepad_plus::Notepad_plus(): Window(), _mainWindowStatus(0), _pDocTab(NULL), _pEditView(NULL),
	_pMainSplitter(NULL),
    _recordingMacro(false), _pTrayIco(NULL), _isUDDocked(false), _isRTL(false),
	_linkTriggered(true), _isDocModifing(false), _isHotspotDblClicked(false), _sysMenuEntering(false),
	_autoCompleteMain(&_mainEditView), _autoCompleteSub(&_subEditView), _smartHighlighter(&_findReplaceDlg),
	_nativeLangEncoding(CP_ACP), _isFileOpening(false), _rememberThisSession(true)
{
	ZeroMemory(&_prevSelectedRange, sizeof(_prevSelectedRange));
	_winVersion = (NppParameters::getInstance())->getWinVersion();

	TiXmlDocumentA *nativeLangDocRootA = (NppParameters::getInstance())->getNativeLangA();

	if (nativeLangDocRootA)
	{

		_nativeLangA =  nativeLangDocRootA->FirstChild("NotepadPlus");
		if (_nativeLangA)
		{
			_nativeLangA = _nativeLangA->FirstChild("Native-Langue");
			if (_nativeLangA)
			{
				TiXmlElementA *element = _nativeLangA->ToElement();
				const char *rtl = element->Attribute("RTL");
				if (rtl)
					_isRTL = (strcmp(rtl, "yes") == 0);

                // get original file name (defined by Notpad++) from the attribute
                const char *fn = element->Attribute("filename");
#ifdef UNICODE
				LocalizationSwitcher & localizationSwitcher = (NppParameters::getInstance())->getLocalizationSwitcher();
                if (fn)
                {
                    localizationSwitcher.setFileName(fn);
                }
#endif
				if (fn && stricmp("english.xml", fn) == 0)
                {
					_nativeLangA = NULL;
					_toolIcons = NULL;
					return;
				}
				// get encoding
				TiXmlDeclarationA *declaration =  _nativeLangA->GetDocument()->FirstChild()->ToDeclaration();
				if (declaration)
				{
					const char * encodingStr = declaration->Encoding();
					EncodingMapper *em = EncodingMapper::getInstance();
                    int enc = em->getEncodingFromString(encodingStr);
                    if (enc != -1)
					    _nativeLangEncoding = enc;
				}
			}	
		}
    }
	else
		_nativeLangA = NULL;
	
	TiXmlDocument *toolIconsDocRoot = (NppParameters::getInstance())->getToolIcons();
	if (toolIconsDocRoot)
	{
		_toolIcons =  toolIconsDocRoot->FirstChild(TEXT("NotepadPlus"));
		if (_toolIcons)
		{
			_toolIcons = _toolIcons->FirstChild(TEXT("ToolBarIcons"));
			if (_toolIcons)
			{
				_toolIcons = _toolIcons->FirstChild(TEXT("Theme"));
				if (_toolIcons)
				{
					const TCHAR *themeDir = (_toolIcons->ToElement())->Attribute(TEXT("pathPrefix"));

					for (TiXmlNode *childNode = _toolIcons->FirstChildElement(TEXT("Icon"));
						 childNode ;
						 childNode = childNode->NextSibling(TEXT("Icon")))
					{
						int iIcon;
						const TCHAR *res = (childNode->ToElement())->Attribute(TEXT("id"), &iIcon);
						if (res)
						{
							TiXmlNode *grandChildNode = childNode->FirstChildElement(TEXT("normal"));
							if (grandChildNode)
							{
								TiXmlNode *valueNode = grandChildNode->FirstChild();
								//putain, enfin!!!
								if (valueNode)
								{
									generic_string locator = themeDir?themeDir:TEXT("");
									
									locator += valueNode->Value();
									_customIconVect.push_back(iconLocator(0, iIcon, locator));
								}
							}

							grandChildNode = childNode->FirstChildElement(TEXT("hover"));
							if (grandChildNode)
							{
								TiXmlNode *valueNode = grandChildNode->FirstChild();
								//putain, enfin!!!
								if (valueNode)
								{
									generic_string locator = themeDir?themeDir:TEXT("");
									
									locator += valueNode->Value();
									_customIconVect.push_back(iconLocator(1, iIcon, locator));
								}
							}

							grandChildNode = childNode->FirstChildElement(TEXT("disabled"));
							if (grandChildNode)
							{
								TiXmlNode *valueNode = grandChildNode->FirstChild();
								//putain, enfin!!!
								if (valueNode)
								{
									generic_string locator = themeDir?themeDir:TEXT("");
									
									locator += valueNode->Value();
									_customIconVect.push_back(iconLocator(2, iIcon, locator));
								}
							}
						}
					}
				}
			}
		}
	}
	else
		_toolIcons = NULL;
}

// ATTENTION : the order of the destruction is very important
// because if the parent's window hadle is destroyed before
// the destruction of its childrens' windows handle, 
// its childrens' windows handle will be destroyed automatically!
Notepad_plus::~Notepad_plus()
{
	(NppParameters::getInstance())->destroyInstance();
	MainFileManager->destroyInstance();
	(WcharMbcsConvertor::getInstance())->destroyInstance();
	if (_pTrayIco)
		delete _pTrayIco;
}

void Notepad_plus::init(HINSTANCE hInst, HWND parent, const TCHAR *cmdLine, CmdLineParams *cmdLineParams)
{
	time_t timestampBegin = 0;
	if (cmdLineParams->_showLoadingTime)
		timestampBegin = time(NULL);

	Window::init(hInst, parent);
	WNDCLASS nppClass;

	nppClass.style = CS_BYTEALIGNWINDOW | CS_DBLCLKS;
	nppClass.lpfnWndProc = Notepad_plus_Proc;
	nppClass.cbClsExtra = 0;
	nppClass.cbWndExtra = 0;
	nppClass.hInstance = _hInst;
	nppClass.hIcon = ::LoadIcon(_hInst, MAKEINTRESOURCE(IDI_M30ICON));
	nppClass.hCursor = ::LoadCursor(NULL, IDC_ARROW);
	nppClass.hbrBackground = ::CreateSolidBrush(::GetSysColor(COLOR_MENU));
	nppClass.lpszMenuName = MAKEINTRESOURCE(IDR_M30_MENU);
	nppClass.lpszClassName = _className;

	_isPrelaunch = cmdLineParams->_isPreLaunch;

	if (!::RegisterClass(&nppClass))
	{
		systemMessage(TEXT("System Err"));
		throw int(98);
	}

	RECT workAreaRect;
	::SystemParametersInfo(SPI_GETWORKAREA, 0, &workAreaRect, 0);

	NppParameters *pNppParams = NppParameters::getInstance();
	const NppGUI & nppGUI = pNppParams->getNppGUI();

	if (cmdLineParams->_isNoPlugin)
		_pluginsManager.disable();

	_hSelf = ::CreateWindowEx(
					WS_EX_ACCEPTFILES | (_isRTL?WS_EX_LAYOUTRTL:0),\
					_className,\
					TEXT("Notepad++"),\
					WS_OVERLAPPEDWINDOW	| WS_CLIPCHILDREN,\
					// CreateWindowEx bug : set all 0 to walk arround the pb
					0, 0, 0, 0,\
					_hParent,\
					NULL,\
					_hInst,\
					(LPVOID)this); // pass the ptr of this instantiated object
                                   // for retrive it in Notepad_plus_Proc from 
                                   // the CREATESTRUCT.lpCreateParams afterward.

	if (!_hSelf)
	{
		systemMessage(TEXT("System Err"));
		throw int(777);
	}

	gNppHWND = _hSelf;

	// In setting the startup window position, take into account that the last-saved
	// position might have assumed a second monitor that's no longer available.
	POINT newUpperLeft;
	newUpperLeft.x = nppGUI._appPos.left + workAreaRect.left;
	newUpperLeft.y = nppGUI._appPos.top + workAreaRect.top;

	// GetSystemMetrics does not support the multi-monitor values on Windows NT and Windows 95.
	if ((_winVersion != WV_95) && (_winVersion != WV_NT)) 
	{
		int margin = ::GetSystemMetrics(SM_CYSMCAPTION);
		if (newUpperLeft.x > ::GetSystemMetrics(SM_CXVIRTUALSCREEN)-margin)
			newUpperLeft.x = workAreaRect.right - nppGUI._appPos.right;
		if (newUpperLeft.x + nppGUI._appPos.right < ::GetSystemMetrics(SM_XVIRTUALSCREEN)+margin)
			newUpperLeft.x = workAreaRect.left;
		if (newUpperLeft.y > ::GetSystemMetrics(SM_CYVIRTUALSCREEN)-margin)
			newUpperLeft.y = workAreaRect.bottom - nppGUI._appPos.bottom;
		if (newUpperLeft.y + nppGUI._appPos.bottom < ::GetSystemMetrics(SM_YVIRTUALSCREEN)+margin)
			newUpperLeft.y = workAreaRect.top;
	}

	if (cmdLineParams->isPointValid())
		::MoveWindow(_hSelf, cmdLineParams->_point.x, cmdLineParams->_point.y, nppGUI._appPos.right, nppGUI._appPos.bottom, TRUE);
	else
		::MoveWindow(_hSelf, newUpperLeft.x, newUpperLeft.y, nppGUI._appPos.right, nppGUI._appPos.bottom, TRUE);
	
	if (nppGUI._tabStatus & TAB_MULTILINE)
		::SendMessage(_hSelf, WM_COMMAND, IDM_VIEW_DRAWTABBAR_MULTILINE, 0);

	if (!nppGUI._menuBarShow)
		::SetMenu(_hSelf, NULL);
	
	if (cmdLineParams->_isNoTab || (nppGUI._tabStatus & TAB_HIDE))
	{
		::SendMessage(_hSelf, NPPM_HIDETABBAR, 0, TRUE);
	}

    _rememberThisSession = !cmdLineParams->_isNoSession;
	if (nppGUI._rememberLastSession && !cmdLineParams->_isNoSession)
	{
		loadLastSession();
	}

	if (!cmdLineParams->_isPreLaunch)
	{
		if (cmdLineParams->isPointValid())
			::ShowWindow(_hSelf, SW_SHOW);
		else
			::ShowWindow(_hSelf, nppGUI._isMaximized?SW_MAXIMIZE:SW_SHOW);
	}
	else
	{
		_pTrayIco = new trayIconControler(_hSelf, IDI_M30ICON, IDC_MINIMIZED_TRAY, ::LoadIcon(_hInst, MAKEINTRESOURCE(IDI_M30ICON)), TEXT(""));
		_pTrayIco->doTrayIcon(ADD);
	}

    if (cmdLine)
    {
		loadCommandlineParams(cmdLine, cmdLineParams);
    }

	vector<generic_string> fileNames;
	vector<generic_string> patterns;
	patterns.push_back(TEXT("*.xml"));
	
	generic_string nppDir = pNppParams->getNppPath();
#ifdef UNICODE
	LocalizationSwitcher & localizationSwitcher = pNppParams->getLocalizationSwitcher();
	wstring localizationDir = nppDir;
	PathAppend(localizationDir, TEXT("localization\\"));

	getMatchedFileNames(localizationDir.c_str(), patterns, fileNames, false, false);
	for (size_t i = 0 ; i < fileNames.size() ; i++)
	{
		localizationSwitcher.addLanguageFromXml(fileNames[i].c_str());
	}
#endif

	fileNames.clear();
	ThemeSwitcher & themeSwitcher = pNppParams->getThemeSwitcher();
	
	//  Get themes from both npp install themes dir and app data themes dir with the per user
	//  overriding default themes of the same name.
	generic_string themeDir(pNppParams->getAppDataNppDir());
	PathAppend(themeDir, TEXT("themes\\"));

	getMatchedFileNames(themeDir.c_str(), patterns, fileNames, false, false);
	for (size_t i = 0 ; i < fileNames.size() ; i++)
	{
		themeSwitcher.addThemeFromXml(fileNames[i].c_str());
	}

	fileNames.clear();
	themeDir.clear();
	themeDir = nppDir.c_str(); // <- should use the pointer to avoid the constructor of copy
	PathAppend(themeDir, TEXT("themes\\"));
	getMatchedFileNames(themeDir.c_str(), patterns, fileNames, false, false);
	for (size_t i = 0 ; i < fileNames.size() ; i++)
	{
		generic_string themeName( themeSwitcher.getThemeFromXmlFileName(fileNames[i].c_str()) );
		if (! themeSwitcher.themeNameExists(themeName.c_str()) ) 
		{
			themeSwitcher.addThemeFromXml(fileNames[i].c_str());
		}
	}

	// Notify plugins that Notepad++ is ready
	SCNotification scnN;
	scnN.nmhdr.code = NPPN_READY;
	scnN.nmhdr.hwndFrom = _hSelf;
	scnN.nmhdr.idFrom = 0;
	_pluginsManager.notify(&scnN);

	if (cmdLineParams->_showLoadingTime)
	{
		time_t timestampEnd = time(NULL);
		double loadTime = difftime(timestampEnd, timestampBegin);

		char dest[256];
		sprintf(dest, "Loading time : %.2lf seconds", loadTime);
		::MessageBoxA(NULL, dest, "", MB_OK);
	}
}



void Notepad_plus::killAllChildren() 
{
	_toolBar.destroy();
	_rebarTop.destroy();
	_rebarBottom.destroy();

    if (_pMainSplitter)
    {
        _pMainSplitter->destroy();
        delete _pMainSplitter;
    }

    _mainDocTab.destroy();
    _subDocTab.destroy();

	_mainEditView.destroy();
    _subEditView.destroy();
	_invisibleEditView.destroy();

    _subSplitter.destroy();
    _statusBar.destroy();

	_scintillaCtrls4Plugins.destroy();
	_dockingManager.destroy();
}

bool Notepad_plus::saveGUIParams()
{
	NppGUI & nppGUI = (NppGUI &)(NppParameters::getInstance())->getNppGUI();
	nppGUI._toolbarShow = _rebarTop.getIDVisible(REBAR_BAR_TOOLBAR);
	nppGUI._toolBarStatus = _toolBar.getState();

	nppGUI._tabStatus = (TabBarPlus::doDragNDropOrNot()?TAB_DRAWTOPBAR:0) | \
						(TabBarPlus::drawTopBar()?TAB_DRAGNDROP:0) | \
						(TabBarPlus::drawInactiveTab()?TAB_DRAWINACTIVETAB:0) | \
						(_toReduceTabBar?TAB_REDUCE:0) | \
						(TabBarPlus::drawTabCloseButton()?TAB_CLOSEBUTTON:0) | \
						(TabBarPlus::isDbClk2Close()?TAB_DBCLK2CLOSE:0) | \
						(TabBarPlus::isVertical() ? TAB_VERTICAL:0) | \
						(TabBarPlus::isMultiLine() ? TAB_MULTILINE:0) |\
						(nppGUI._tabStatus & TAB_HIDE);
	nppGUI._splitterPos = _subSplitter.isVertical()?POS_VERTICAL:POS_HORIZOTAL;
	UserDefineDialog *udd = _pEditView->getUserDefineDlg();
	bool b = udd->isDocked();
	nppGUI._userDefineDlgStatus = (b?UDD_DOCKED:0) | (udd->isVisible()?UDD_SHOW:0);
	
	// Save the position

	WINDOWPLACEMENT posInfo;

    posInfo.length = sizeof(WINDOWPLACEMENT);
	::GetWindowPlacement(_hSelf, &posInfo);

	nppGUI._appPos.left = posInfo.rcNormalPosition.left;
	nppGUI._appPos.top = posInfo.rcNormalPosition.top;
	nppGUI._appPos.right = posInfo.rcNormalPosition.right - posInfo.rcNormalPosition.left;
	nppGUI._appPos.bottom = posInfo.rcNormalPosition.bottom - posInfo.rcNormalPosition.top;
	nppGUI._isMaximized = (IsZoomed(_hSelf) || (posInfo.flags & WPF_RESTORETOMAXIMIZED));

	saveDockingParams();

	return (NppParameters::getInstance())->writeGUIParams();
}

void Notepad_plus::saveDockingParams() 
{
	NppGUI & nppGUI = (NppGUI &)(NppParameters::getInstance())->getNppGUI();

	// Save the docking information
	nppGUI._dockingData._leftWidth		= _dockingManager.getDockedContSize(CONT_LEFT);
	nppGUI._dockingData._rightWidth		= _dockingManager.getDockedContSize(CONT_RIGHT); 
	nppGUI._dockingData._topHeight		= _dockingManager.getDockedContSize(CONT_TOP);	 
	nppGUI._dockingData._bottomHight	= _dockingManager.getDockedContSize(CONT_BOTTOM);

	// clear the conatainer tab information (active tab)
	nppGUI._dockingData._containerTabInfo.clear();

	// create a vector to save the current information
	vector<PlugingDlgDockingInfo>	vPluginDockInfo;
	vector<FloatingWindowInfo>		vFloatingWindowInfo;

	// save every container
	vector<DockingCont*> vCont = _dockingManager.getContainerInfo();

	for (size_t i = 0 ; i < vCont.size() ; i++)
	{
		// save at first the visible Tb's
		vector<tTbData *>	vDataVis	= vCont[i]->getDataOfVisTb();

		for (size_t j = 0 ; j < vDataVis.size() ; j++)
		{
			if (vDataVis[j]->pszName && vDataVis[j]->pszName[0])
			{
				PlugingDlgDockingInfo pddi(vDataVis[j]->pszModuleName, vDataVis[j]->dlgID, i, vDataVis[j]->iPrevCont, true);
				vPluginDockInfo.push_back(pddi);
			}
		}

		// save the hidden Tb's
		vector<tTbData *>	vDataAll	= vCont[i]->getDataOfAllTb();

		for (size_t j = 0 ; j < vDataAll.size() ; j++)
		{
			if ((vDataAll[j]->pszName && vDataAll[j]->pszName[0]) && (!vCont[i]->isTbVis(vDataAll[j])))
			{
				PlugingDlgDockingInfo pddi(vDataAll[j]->pszModuleName, vDataAll[j]->dlgID, i, vDataAll[j]->iPrevCont, false);
				vPluginDockInfo.push_back(pddi);
			}
		}

		// save the position, when container is a floated one
		if (i >= DOCKCONT_MAX)
		{
			RECT	rc;
			vCont[i]->getWindowRect(rc);
			FloatingWindowInfo fwi(i, rc.left, rc.top, rc.right, rc.bottom);
			vFloatingWindowInfo.push_back(fwi);
		}

		// save the active tab
		ContainerTabInfo act(i, vCont[i]->getActiveTb());
		nppGUI._dockingData._containerTabInfo.push_back(act);
	}

	// add the missing information and store it in nppGUI
	UCHAR floatContArray[50];
	memset(floatContArray, 0, 50);

	for (size_t i = 0 ; i < nppGUI._dockingData._pluginDockInfo.size() ; i++)
	{
		BOOL	isStored = FALSE;
		for (size_t j = 0; j < vPluginDockInfo.size(); j++)
		{
			if (nppGUI._dockingData._pluginDockInfo[i] == vPluginDockInfo[j])
			{
				isStored = TRUE;
				break;
			}
		}

		if (isStored == FALSE)
		{
			int floatCont	= 0;

			if (nppGUI._dockingData._pluginDockInfo[i]._currContainer >= DOCKCONT_MAX)
				floatCont = nppGUI._dockingData._pluginDockInfo[i]._currContainer;
			else
				floatCont = nppGUI._dockingData._pluginDockInfo[i]._prevContainer;

			if (floatContArray[floatCont] == 0)
			{
				RECT *pRc = nppGUI._dockingData.getFloatingRCFrom(floatCont);
				if (pRc)
					vFloatingWindowInfo.push_back(FloatingWindowInfo(floatCont, pRc->left, pRc->top, pRc->right, pRc->bottom));
				floatContArray[floatCont] = 1;
			}

			vPluginDockInfo.push_back(nppGUI._dockingData._pluginDockInfo[i]);
		}
	}

	nppGUI._dockingData._pluginDockInfo = vPluginDockInfo;
	nppGUI._dockingData._flaotingWindowInfo = vFloatingWindowInfo;
}


int Notepad_plus::getHtmlXmlEncoding(const TCHAR *fileName) const
{
	// Get Language type
	TCHAR *ext = PathFindExtension(fileName);
	if (*ext == '.') //extension found
	{
		ext += 1;
	}
	else
	{
		return -1;
	}
	NppParameters *pNppParamInst = NppParameters::getInstance();
	LangType langT = pNppParamInst->getLangFromExt(ext);
	if (langT != L_XML && langT != L_HTML && langT == L_PHP)
		return -1;

	// Get the begining of file data
	FILE *f = generic_fopen(fileName, TEXT("rb"));
	if (!f)
		return -1;
	const int blockSize = 1024; // To ensure that length is long enough to capture the encoding in html
	char data[blockSize];
	int lenFile = fread(data, 1, blockSize, f);
	fclose(f);
	
	// Put data in _invisibleEditView
	_invisibleEditView.execute(SCI_CLEARALL);
    _invisibleEditView.execute(SCI_APPENDTEXT, lenFile, (LPARAM)data);

	const char *encodingAliasRegExpr = "[a-zA-Z0-9_-]+";

	if (langT == L_XML)
	{
		// find encoding by RegExpr
	
		const char *xmlHeaderRegExpr = "<?xml[ \\t]+version[ \\t]*=[ \\t]*\"[^\"]+\"[ \\t]+encoding[ \\t]*=[ \\t]*\"[^\"]+\"[ \\t]*.*?>";
        
        int startPos = 0;
		int endPos = lenFile-1;
		_invisibleEditView.execute(SCI_SETSEARCHFLAGS, SCFIND_REGEXP|SCFIND_POSIX);

		_invisibleEditView.execute(SCI_SETTARGETSTART, startPos);
		_invisibleEditView.execute(SCI_SETTARGETEND, endPos);
	
		int posFound = _invisibleEditView.execute(SCI_SEARCHINTARGET, strlen(xmlHeaderRegExpr), (LPARAM)xmlHeaderRegExpr);
		if (posFound != -1)
		{
            const char *encodingBlockRegExpr = "encoding[ \\t]*=[ \\t]*\"[^\".]+\"";
            posFound = _invisibleEditView.execute(SCI_SEARCHINTARGET, strlen(encodingBlockRegExpr), (LPARAM)encodingBlockRegExpr);

            const char *encodingRegExpr = "\".+\"";
            posFound = _invisibleEditView.execute(SCI_SEARCHINTARGET, strlen(encodingRegExpr), (LPARAM)encodingRegExpr);

			posFound = _invisibleEditView.execute(SCI_SEARCHINTARGET, strlen(encodingAliasRegExpr), (LPARAM)encodingAliasRegExpr);

            startPos = int(_invisibleEditView.execute(SCI_GETTARGETSTART));
			endPos = int(_invisibleEditView.execute(SCI_GETTARGETEND));

            char encodingStr[128];
            _invisibleEditView.getText(encodingStr, startPos, endPos);

			EncodingMapper *em = EncodingMapper::getInstance();
            int enc = em->getEncodingFromString(encodingStr);
            return (enc==CP_ACP?-1:enc);
		}
        return -1;
	}
	else // if (langT == L_HTML)
	{
		const char *htmlHeaderRegExpr  = "<meta[ \\t]+http-equiv[ \\t]*=[ \\t\"']*Content-Type[ \\t\"']*content[ \\t]*= *[\"']text/html;[ \\t]+charset[ \\t]*=[ \\t]*.+[\"'] */*>";
		const char *htmlHeaderRegExpr2 = "<meta[ \\t]+content[ \\t]*= *[\"']text/html;[ \\t]+charset[ \\t]*=[ \\t]*.+[ \\t\"']http-equiv[ \\t]*=[ \\t\"']*Content-Type[ \\t\"']*/*>";
		const char *charsetBlock = "charset[ \\t]*=[ \\t]*[^\"']+";
		const char *intermediaire = "=[ \\t]*.+";
		const char *encodingStrRE = "[^ \\t=]+";

        int startPos = 0;
		int endPos = lenFile-1;
		_invisibleEditView.execute(SCI_SETSEARCHFLAGS, SCFIND_REGEXP|SCFIND_POSIX);

		_invisibleEditView.execute(SCI_SETTARGETSTART, startPos);
		_invisibleEditView.execute(SCI_SETTARGETEND, endPos);

		int posFound = _invisibleEditView.execute(SCI_SEARCHINTARGET, strlen(htmlHeaderRegExpr), (LPARAM)htmlHeaderRegExpr);

		if (posFound == -1)
		{
			posFound = _invisibleEditView.execute(SCI_SEARCHINTARGET, strlen(htmlHeaderRegExpr2), (LPARAM)htmlHeaderRegExpr2);
			if (posFound == -1)
				return -1;
		}
		posFound = _invisibleEditView.execute(SCI_SEARCHINTARGET, strlen(charsetBlock), (LPARAM)charsetBlock);
		posFound = _invisibleEditView.execute(SCI_SEARCHINTARGET, strlen(intermediaire), (LPARAM)intermediaire);
		posFound = _invisibleEditView.execute(SCI_SEARCHINTARGET, strlen(encodingStrRE), (LPARAM)encodingStrRE);

        startPos = int(_invisibleEditView.execute(SCI_GETTARGETSTART));
		endPos = int(_invisibleEditView.execute(SCI_GETTARGETEND));

        char encodingStr[128];
        _invisibleEditView.getText(encodingStr, startPos, endPos);

		EncodingMapper *em = EncodingMapper::getInstance();
		int enc = em->getEncodingFromString(encodingStr);
        return (enc==CP_ACP?-1:enc);
	}
}


bool Notepad_plus::replaceAllFiles() {

	ScintillaEditView *pOldView = _pEditView;
	_pEditView = &_invisibleEditView;
	Document oldDoc = _invisibleEditView.execute(SCI_GETDOCPOINTER);
	Buffer * oldBuf = _invisibleEditView.getCurrentBuffer();	//for manually setting the buffer, so notifications can be handled properly

	Buffer * pBuf = NULL;

	int nbTotal = 0;
	const bool isEntireDoc = true;

    if (_mainWindowStatus & WindowMainActive)
    {
		for (int i = 0 ; i < _mainDocTab.nbItem() ; i++)
	    {
			pBuf = MainFileManager->getBufferByID(_mainDocTab.getBufferByIndex(i));
			if (pBuf->isReadOnly())
				continue;
			_invisibleEditView.execute(SCI_SETDOCPOINTER, 0, pBuf->getDocument());
			int cp = _invisibleEditView.execute(SCI_GETCODEPAGE);
			_invisibleEditView.execute(SCI_SETCODEPAGE, pBuf->getUnicodeMode() == uni8Bit ? cp : SC_CP_UTF8);
			_invisibleEditView._currentBuffer = pBuf;
		    _invisibleEditView.execute(SCI_BEGINUNDOACTION);
			nbTotal += _findReplaceDlg.processAll(ProcessReplaceAll, NULL, NULL, isEntireDoc, NULL);
			_invisibleEditView.execute(SCI_ENDUNDOACTION);
		}
	}
	
	if (_mainWindowStatus & WindowSubActive)
    {
		for (int i = 0 ; i < _subDocTab.nbItem() ; i++)
	    {
			pBuf = MainFileManager->getBufferByID(_subDocTab.getBufferByIndex(i));
			if (pBuf->isReadOnly())
				continue;
			_invisibleEditView.execute(SCI_SETDOCPOINTER, 0, pBuf->getDocument());
			int cp = _invisibleEditView.execute(SCI_GETCODEPAGE);
			_invisibleEditView.execute(SCI_SETCODEPAGE, pBuf->getUnicodeMode() == uni8Bit ? cp : SC_CP_UTF8);
			_invisibleEditView._currentBuffer = pBuf;
		    _invisibleEditView.execute(SCI_BEGINUNDOACTION);
			nbTotal += _findReplaceDlg.processAll(ProcessReplaceAll, NULL, NULL, isEntireDoc, NULL);
			_invisibleEditView.execute(SCI_ENDUNDOACTION);
		}
	}

	_invisibleEditView.execute(SCI_SETDOCPOINTER, 0, oldDoc);
	_invisibleEditView._currentBuffer = oldBuf;
	_pEditView = pOldView;

	
	if (nbTotal < 0)
		::printStr(TEXT("The regular expression to search is formed badly"));
	else
	{
		if (nbTotal)
			enableCommand(IDM_FILE_SAVEALL, true, MENU | TOOLBAR);
		TCHAR result[64];
		wsprintf(result, TEXT("%d occurrences replaced."), nbTotal);
		::printStr(result);
	}
	

	return true;
}

bool Notepad_plus::matchInList(const TCHAR *fileName, const vector<generic_string> & patterns)
{
	for (size_t i = 0 ; i < patterns.size() ; i++)
	{
		if (PathMatchSpec(fileName, patterns[i].c_str()))
			return true;
	}
	return false;
}

void Notepad_plus::saveFindHistory()
{
	_findReplaceDlg.saveFindHistory();
	(NppParameters::getInstance())->writeFindHistory();
}

void Notepad_plus::saveUserDefineLangs() 
{
	if (ScintillaEditView::getUserDefineDlg()->isDirty())
		(NppParameters::getInstance())->writeUserDefinedLang();
}

void Notepad_plus::saveShortcuts()
{
	NppParameters::getInstance()->writeShortcuts();
}

void Notepad_plus::doTrimTrailing() 
{
	_pEditView->execute(SCI_BEGINUNDOACTION);
	int nbLines = _pEditView->execute(SCI_GETLINECOUNT);
	for (int line = 0 ; line < nbLines ; line++)
	{
		int lineStart = _pEditView->execute(SCI_POSITIONFROMLINE,line);
		int lineEnd = _pEditView->execute(SCI_GETLINEENDPOSITION,line);
		int i = lineEnd - 1;
		char c = (char)_pEditView->execute(SCI_GETCHARAT,i);

		for ( ; (i >= lineStart) && (c == ' ') || (c == '\t') ; c = (char)_pEditView->execute(SCI_GETCHARAT,i))
			i--;

		if (i < (lineEnd - 1))
			_pEditView->replaceTarget(TEXT(""), i + 1, lineEnd);
	}
	_pEditView->execute(SCI_ENDUNDOACTION);
}

void Notepad_plus::getMatchedFileNames(const TCHAR *dir, const vector<generic_string> & patterns, vector<generic_string> & fileNames, bool isRecursive, bool isInHiddenDir)
{
	generic_string dirFilter(dir);
	dirFilter += TEXT("*.*");
	WIN32_FIND_DATA foundData;

	HANDLE hFile = ::FindFirstFile(dirFilter.c_str(), &foundData);

	if (hFile != INVALID_HANDLE_VALUE)
	{
		
		if (foundData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			if (!isInHiddenDir && (foundData.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN))
			{
				// branles rien
			}
			else if (isRecursive)
			{
				if ((lstrcmp(foundData.cFileName, TEXT("."))) && (lstrcmp(foundData.cFileName, TEXT(".."))))
				{
					generic_string pathDir(dir);
					pathDir += foundData.cFileName;
					pathDir += TEXT("\\");
					getMatchedFileNames(pathDir.c_str(), patterns, fileNames, isRecursive, isInHiddenDir);
				}
			}
		}
		else
		{
			if (matchInList(foundData.cFileName, patterns))
			{
				generic_string pathFile(dir);
				pathFile += foundData.cFileName;
				fileNames.push_back(pathFile.c_str());
			}
		}
	}
	while (::FindNextFile(hFile, &foundData))
	{
		if (foundData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			if (!isInHiddenDir && (foundData.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN))
			{
				// branles rien
			}
			else if (isRecursive)
			{
				if ((lstrcmp(foundData.cFileName, TEXT("."))) && (lstrcmp(foundData.cFileName, TEXT(".."))))
				{
					generic_string pathDir(dir);
					pathDir += foundData.cFileName;
					pathDir += TEXT("\\");
					getMatchedFileNames(pathDir.c_str(), patterns, fileNames, isRecursive, isInHiddenDir);
				}
			}
		}
		else
		{
			if (matchInList(foundData.cFileName, patterns))
			{
				generic_string pathFile(dir);
				pathFile += foundData.cFileName;
				fileNames.push_back(pathFile.c_str());
			}
		}
	}
	::FindClose(hFile);
}

DWORD WINAPI AsyncCancelFindInFiles(LPVOID NppHWND) 
{
	MessageBox((HWND) NULL, TEXT("Searching...\nPress Enter to Cancel"), TEXT("Find In Files"), MB_OK);
	PostMessage((HWND) NppHWND, NPPM_INTERNAL_CANCEL_FIND_IN_FILES, 0, 0);
	return 0;
}

bool Notepad_plus::replaceInFiles()
{
	const TCHAR *dir2Search = _findReplaceDlg.getDir2Search();
	if (!dir2Search[0] || !::PathFileExists(dir2Search))
	{
		return false;
	}

	bool isRecursive = _findReplaceDlg.isRecursive();
	bool isInHiddenDir = _findReplaceDlg.isInHiddenDir();
	int nbTotal = 0;

	ScintillaEditView *pOldView = _pEditView;
	_pEditView = &_invisibleEditView;
	Document oldDoc = _invisibleEditView.execute(SCI_GETDOCPOINTER);
	Buffer * oldBuf = _invisibleEditView.getCurrentBuffer();	//for manually setting the buffer, so notifications can be handled properly
	HANDLE CancelThreadHandle = NULL;

	vector<generic_string> patterns2Match;
	_findReplaceDlg.getPatterns(patterns2Match);
	if (patterns2Match.size() == 0)
	{
		_findReplaceDlg.setFindInFilesDirFilter(NULL, TEXT("*.*"));
		_findReplaceDlg.getPatterns(patterns2Match);
	}
	vector<generic_string> fileNames;

	getMatchedFileNames(dir2Search, patterns2Match, fileNames, isRecursive, isInHiddenDir);

	if (fileNames.size() > 1)
		CancelThreadHandle = ::CreateThread(NULL, 0, AsyncCancelFindInFiles, _hSelf, 0, NULL);

	bool dontClose = false;
	for (size_t i = 0 ; i < fileNames.size() ; i++)
	{
		MSG msg;
		if (PeekMessage(&msg, _hSelf, NPPM_INTERNAL_CANCEL_FIND_IN_FILES, NPPM_INTERNAL_CANCEL_FIND_IN_FILES, PM_REMOVE)) break;

		BufferID id = MainFileManager->getBufferFromName(fileNames.at(i).c_str());
		if (id != BUFFER_INVALID) 
		{
			dontClose = true;
		} 
		else 
		{
			id = MainFileManager->loadFile(fileNames.at(i).c_str());
			dontClose = false;
		}
		
		if (id != BUFFER_INVALID) 
		{
			Buffer * pBuf = MainFileManager->getBufferByID(id);
			_invisibleEditView.execute(SCI_SETDOCPOINTER, 0, pBuf->getDocument());
			int cp = _invisibleEditView.execute(SCI_GETCODEPAGE);
			_invisibleEditView.execute(SCI_SETCODEPAGE, pBuf->getUnicodeMode() == uni8Bit ? cp : SC_CP_UTF8);
			_invisibleEditView._currentBuffer = pBuf;

			int nbReplaced = _findReplaceDlg.processAll(ProcessReplaceAll, NULL, NULL, true, fileNames.at(i).c_str());
			nbTotal += nbReplaced;
			if (nbReplaced)
			{
				MainFileManager->saveBuffer(id, pBuf->getFullPathName());
			}

			if (!dontClose)
				MainFileManager->closeBuffer(id, _pEditView);
		}
	}

	if (CancelThreadHandle)
		TerminateThread(CancelThreadHandle, 0);

	_invisibleEditView.execute(SCI_SETDOCPOINTER, 0, oldDoc);
	_invisibleEditView._currentBuffer = oldBuf;
	_pEditView = pOldView;
	
	TCHAR msg[128];
	wsprintf(msg, TEXT("%d occurences replaced"), nbTotal);
	printStr(msg);

	return true;
}

bool Notepad_plus::findInFiles()
{
	const TCHAR *dir2Search = _findReplaceDlg.getDir2Search();

	if (!dir2Search[0] || !::PathFileExists(dir2Search))
	{
		return false;
	}

	bool isRecursive = _findReplaceDlg.isRecursive();
	bool isInHiddenDir = _findReplaceDlg.isInHiddenDir();
	int nbTotal = 0;
	ScintillaEditView *pOldView = _pEditView;
	_pEditView = &_invisibleEditView;
	Document oldDoc = _invisibleEditView.execute(SCI_GETDOCPOINTER);
	HANDLE CancelThreadHandle = NULL;

	vector<generic_string> patterns2Match;
	_findReplaceDlg.getPatterns(patterns2Match);
	if (patterns2Match.size() == 0)
	{
		_findReplaceDlg.setFindInFilesDirFilter(NULL, TEXT("*.*"));
		_findReplaceDlg.getPatterns(patterns2Match);
	}
	vector<generic_string> fileNames;
	getMatchedFileNames(dir2Search, patterns2Match, fileNames, isRecursive, isInHiddenDir);

	if (fileNames.size() > 1)
		CancelThreadHandle = ::CreateThread(NULL, 0, AsyncCancelFindInFiles, _hSelf, 0, NULL);

	_findReplaceDlg.beginNewFilesSearch();

	bool dontClose = false;
	for (size_t i = 0 ; i < fileNames.size() ; i++)
	{
		MSG msg;
		if (PeekMessage(&msg, _hSelf, NPPM_INTERNAL_CANCEL_FIND_IN_FILES, NPPM_INTERNAL_CANCEL_FIND_IN_FILES, PM_REMOVE)) break;

		BufferID id = MainFileManager->getBufferFromName(fileNames.at(i).c_str());
		if (id != BUFFER_INVALID) 
		{
			dontClose = true;
		} 
		else 
		{
			id = MainFileManager->loadFile(fileNames.at(i).c_str());
			dontClose = false;
		}
		
		if (id != BUFFER_INVALID) 
		{
			Buffer * pBuf = MainFileManager->getBufferByID(id);
			_invisibleEditView.execute(SCI_SETDOCPOINTER, 0, pBuf->getDocument());
			int cp = _invisibleEditView.execute(SCI_GETCODEPAGE);
			_invisibleEditView.execute(SCI_SETCODEPAGE, pBuf->getUnicodeMode() == uni8Bit ? cp : SC_CP_UTF8);
			
			nbTotal += _findReplaceDlg.processAll(ProcessFindAll, NULL, NULL, true, fileNames.at(i).c_str());
			if (!dontClose)
				MainFileManager->closeBuffer(id, _pEditView);
		}
	}

	if (CancelThreadHandle)
		TerminateThread(CancelThreadHandle, 0);

	_findReplaceDlg.finishFilesSearch(nbTotal);

	_invisibleEditView.execute(SCI_SETDOCPOINTER, 0, oldDoc);
	_pEditView = pOldView;
	
	_findReplaceDlg.putFindResult(nbTotal);

	FindHistory & findHistory = (NppParameters::getInstance())->getFindHistory();
	if (nbTotal && !findHistory._isDlgAlwaysVisible) 
		_findReplaceDlg.display(false);
	return true;
}


bool Notepad_plus::findInOpenedFiles()
{
	int nbTotal = 0;
	ScintillaEditView *pOldView = _pEditView;
	_pEditView = &_invisibleEditView;
	Document oldDoc = _invisibleEditView.execute(SCI_GETDOCPOINTER);

	Buffer * pBuf = NULL;

	const bool isEntireDoc = true;

	_findReplaceDlg.beginNewFilesSearch();
	
    if (_mainWindowStatus & WindowMainActive)
    {
		for (int i = 0 ; i < _mainDocTab.nbItem() ; i++)
	    {
			pBuf = MainFileManager->getBufferByID(_mainDocTab.getBufferByIndex(i));
			_invisibleEditView.execute(SCI_SETDOCPOINTER, 0, pBuf->getDocument());
			int cp = _invisibleEditView.execute(SCI_GETCODEPAGE);
			_invisibleEditView.execute(SCI_SETCODEPAGE, pBuf->getUnicodeMode() == uni8Bit ? cp : SC_CP_UTF8);
			nbTotal += _findReplaceDlg.processAll(ProcessFindAll, NULL, NULL, isEntireDoc, pBuf->getFullPathName());
	    }
    }
    
    if (_mainWindowStatus & WindowSubActive)
    {
		for (int i = 0 ; i < _subDocTab.nbItem() ; i++)
	    {
			pBuf = MainFileManager->getBufferByID(_subDocTab.getBufferByIndex(i));
			_invisibleEditView.execute(SCI_SETDOCPOINTER, 0, pBuf->getDocument());
			int cp = _invisibleEditView.execute(SCI_GETCODEPAGE);
			_invisibleEditView.execute(SCI_SETCODEPAGE, pBuf->getUnicodeMode() == uni8Bit ? cp : SC_CP_UTF8);
			nbTotal += _findReplaceDlg.processAll(ProcessFindAll, NULL, NULL, isEntireDoc, pBuf->getFullPathName());
	    }
    }

	_findReplaceDlg.finishFilesSearch(nbTotal);

	_invisibleEditView.execute(SCI_SETDOCPOINTER, 0, oldDoc);
	_pEditView = pOldView;

	_findReplaceDlg.putFindResult(nbTotal);

	FindHistory & findHistory = (NppParameters::getInstance())->getFindHistory();
	if (nbTotal && !findHistory._isDlgAlwaysVisible)
		_findReplaceDlg.display(false);
	return true;
}


bool Notepad_plus::findInCurrentFile()
{
	int nbTotal = 0;
	Buffer * pBuf = _pEditView->getCurrentBuffer();
	ScintillaEditView *pOldView = _pEditView;
	_pEditView = &_invisibleEditView;
	Document oldDoc = _invisibleEditView.execute(SCI_GETDOCPOINTER);

	const bool isEntireDoc = true;

	_findReplaceDlg.beginNewFilesSearch();
	
	_invisibleEditView.execute(SCI_SETDOCPOINTER, 0, pBuf->getDocument());
	int cp = _invisibleEditView.execute(SCI_GETCODEPAGE);
	_invisibleEditView.execute(SCI_SETCODEPAGE, pBuf->getUnicodeMode() == uni8Bit ? cp : SC_CP_UTF8);
	nbTotal += _findReplaceDlg.processAll(ProcessFindAll, NULL, NULL, isEntireDoc, pBuf->getFullPathName());

	_findReplaceDlg.finishFilesSearch(nbTotal);

	_invisibleEditView.execute(SCI_SETDOCPOINTER, 0, oldDoc);
	_pEditView = pOldView;

	_findReplaceDlg.putFindResult(nbTotal);

	FindHistory & findHistory = (NppParameters::getInstance())->getFindHistory();
	if (nbTotal && !findHistory._isDlgAlwaysVisible) 
		_findReplaceDlg.display(false);
	return true;
}

void Notepad_plus::filePrint(bool showDialog)
{
	Printer printer;

	int startPos = int(_pEditView->execute(SCI_GETSELECTIONSTART));
	int endPos = int(_pEditView->execute(SCI_GETSELECTIONEND));
	
	printer.init(_hInst, _hSelf, _pEditView, showDialog, startPos, endPos);
	printer.doPrint();
}

int Notepad_plus::doSaveOrNot(const TCHAR *fn) 
{
	TCHAR pattern[64] = TEXT("Save file \"%s\" ?");
	TCHAR phrase[512];
	wsprintf(phrase, pattern, fn);
	return doActionOrNot(TEXT("Save"), phrase, MB_YESNOCANCEL | MB_ICONQUESTION | MB_APPLMODAL);
}

int Notepad_plus::doReloadOrNot(const TCHAR *fn, bool dirty) 
{
	TCHAR* pattern = TEXT("%s\r\rThis file has been modified by another program.\rDo you want to reload it%s?");
	TCHAR* lose_info_str = dirty ? TEXT(" and lose the changes made in Notepad++") : TEXT("");
	TCHAR phrase[512];
	wsprintf(phrase, pattern, fn, lose_info_str);
	int icon = dirty ? MB_ICONEXCLAMATION : MB_ICONQUESTION;
	return doActionOrNot(TEXT("Reload"), phrase, MB_YESNO | MB_APPLMODAL | icon);
}

int Notepad_plus::doCloseOrNot(const TCHAR *fn) 
{
	TCHAR pattern[128] = TEXT("The file \"%s\" doesn't exist anymore.\rKeep this file in editor?");
	TCHAR phrase[512];
	wsprintf(phrase, pattern, fn);
	return doActionOrNot(TEXT("Keep non existing file"), phrase, MB_YESNO | MB_ICONQUESTION | MB_APPLMODAL);
}

int Notepad_plus::doDeleteOrNot(const TCHAR *fn) 
{
	TCHAR pattern[128] = TEXT("The file \"%s\"\rwill be deleted from your disk and this document will be closed.\rContinue?");
	TCHAR phrase[512];
	wsprintf(phrase, pattern, fn);
	return doActionOrNot(TEXT("Delete file"), phrase, MB_YESNO | MB_ICONQUESTION | MB_APPLMODAL);
}

int Notepad_plus::doActionOrNot(const TCHAR *title, const TCHAR *displayText, int type) 
{
	return ::MessageBox(_hSelf, displayText, title, type);
}

void Notepad_plus::enableMenu(int cmdID, bool doEnable) const 
{
	int flag = doEnable?MF_ENABLED | MF_BYCOMMAND:MF_DISABLED | MF_GRAYED | MF_BYCOMMAND;
	::EnableMenuItem(_mainMenuHandle, cmdID, flag);
}

void Notepad_plus::enableCommand(int cmdID, bool doEnable, int which) const
{
	if (which & MENU)
	{
		enableMenu(cmdID, doEnable);
	}
	if (which & TOOLBAR)
	{
		_toolBar.enable(cmdID, doEnable);
	}
}

void Notepad_plus::checkClipboard() 
{
	
	bool hasSelection = (_pEditView->execute(SCI_GETSELECTIONSTART) != _pEditView->execute(SCI_GETSELECTIONEND)) || (_pEditView->execute(SCI_GETSELECTIONS) > 0);
	bool canPaste = (_pEditView->execute(SCI_CANPASTE) != 0);
	enableCommand(IDM_EDIT_CUT, hasSelection, MENU | TOOLBAR); 
	enableCommand(IDM_EDIT_COPY, hasSelection, MENU | TOOLBAR);
	enableCommand(IDM_EDIT_PASTE, canPaste, MENU | TOOLBAR);
	enableCommand(IDM_EDIT_DELETE, hasSelection, MENU | TOOLBAR);
	enableCommand(IDM_EDIT_UPPERCASE, hasSelection, MENU);
	enableCommand(IDM_EDIT_LOWERCASE, hasSelection, MENU);
	enableCommand(IDM_EDIT_BLOCK_COMMENT, hasSelection, MENU);
	enableCommand(IDM_EDIT_BLOCK_COMMENT_SET, hasSelection, MENU);
	enableCommand(IDM_EDIT_BLOCK_UNCOMMENT, hasSelection, MENU);
	enableCommand(IDM_EDIT_STREAM_COMMENT, hasSelection, MENU);
}

void Notepad_plus::checkDocState()
{
	Buffer * curBuf = _pEditView->getCurrentBuffer();

	bool isCurrentDirty = curBuf->isDirty();
	bool isSeveralDirty = isCurrentDirty;
	bool isFileExisting = PathFileExists(curBuf->getFullPathName()) != FALSE;
	if (!isCurrentDirty)
	{
		for(int i = 0; i < MainFileManager->getNrBuffers(); i++)
		{
			if (MainFileManager->getBufferByIndex(i)->isDirty())
			{
				isSeveralDirty = true;
				break;
			}
		}
	}

	bool isCurrentUntitled = curBuf->isUntitled();
	enableCommand(IDM_FILE_SAVE, isCurrentDirty, MENU | TOOLBAR);
	enableCommand(IDM_FILE_SAVEALL, isSeveralDirty, MENU | TOOLBAR);
	enableCommand(IDM_VIEW_GOTO_NEW_INSTANCE, !(isCurrentDirty || isCurrentUntitled), MENU);
	enableCommand(IDM_VIEW_LOAD_IN_NEW_INSTANCE, !(isCurrentDirty || isCurrentUntitled), MENU);
	
	bool isSysReadOnly = curBuf->getFileReadOnly();
	if (isSysReadOnly)
	{
		::CheckMenuItem(_mainMenuHandle, IDM_EDIT_SETREADONLY, MF_BYCOMMAND | MF_UNCHECKED);
		enableCommand(IDM_EDIT_SETREADONLY, false, MENU);
		enableCommand(IDM_EDIT_CLEARREADONLY, true, MENU);
	}
	else
	{
		enableCommand(IDM_EDIT_SETREADONLY, true, MENU);
		enableCommand(IDM_EDIT_CLEARREADONLY, false, MENU);
		bool isUserReadOnly = curBuf->getUserReadOnly();
		::CheckMenuItem(_mainMenuHandle, IDM_EDIT_SETREADONLY, MF_BYCOMMAND | (isUserReadOnly?MF_CHECKED:MF_UNCHECKED));
	}
	enableCommand(IDM_FILE_DELETE, isFileExisting, MENU);
	enableCommand(IDM_FILE_RENAME, isFileExisting, MENU);

	enableConvertMenuItems(curBuf->getFormat());
	checkUnicodeMenuItems(/*curBuf->getUnicodeMode()*/);
	checkLangsMenu(-1);
}

void Notepad_plus::checkUndoState()
{
	enableCommand(IDM_EDIT_UNDO, _pEditView->execute(SCI_CANUNDO) != 0, MENU | TOOLBAR);
	enableCommand(IDM_EDIT_REDO, _pEditView->execute(SCI_CANREDO) != 0, MENU | TOOLBAR);
}

void Notepad_plus::checkMacroState()
{
	enableCommand(IDM_MACRO_STARTRECORDINGMACRO, !_recordingMacro, MENU | TOOLBAR);
	enableCommand(IDM_MACRO_STOPRECORDINGMACRO, _recordingMacro, MENU | TOOLBAR);
	enableCommand(IDM_MACRO_PLAYBACKRECORDEDMACRO, !_macro.empty() && !_recordingMacro, MENU | TOOLBAR);
	enableCommand(IDM_MACRO_SAVECURRENTMACRO, !_macro.empty() && !_recordingMacro, MENU | TOOLBAR);
	
	enableCommand(IDM_MACRO_RUNMULTIMACRODLG, (!_macro.empty() && !_recordingMacro) || !((NppParameters::getInstance())->getMacroList()).empty(), MENU | TOOLBAR);
}

void Notepad_plus::checkSyncState()
{
	bool canDoSync = viewVisible(MAIN_VIEW) && viewVisible(SUB_VIEW);
	if (!canDoSync)
	{
		_syncInfo._isSynScollV = false;
		_syncInfo._isSynScollH = false;
		checkMenuItem(IDM_VIEW_SYNSCROLLV, false);
		checkMenuItem(IDM_VIEW_SYNSCROLLH, false);
		_toolBar.setCheck(IDM_VIEW_SYNSCROLLV, false);
		_toolBar.setCheck(IDM_VIEW_SYNSCROLLH, false);
	}
	enableCommand(IDM_VIEW_SYNSCROLLV, canDoSync, MENU | TOOLBAR);
	enableCommand(IDM_VIEW_SYNSCROLLH, canDoSync, MENU | TOOLBAR);
}

void Notepad_plus::checkLangsMenu(int id) const 
{
	Buffer * curBuf = _pEditView->getCurrentBuffer();
	if (id == -1)
	{
		id = (NppParameters::getInstance())->langTypeToCommandID(curBuf->getLangType());
		if (id == IDM_LANG_USER)
		{
			if (curBuf->isUserDefineLangExt())
			{
				const TCHAR *userLangName = curBuf->getUserDefineLangName();
				const int nbChar = 16;
				TCHAR menuLangName[nbChar];

				for (int i = IDM_LANG_USER + 1 ; i <= IDM_LANG_USER_LIMIT ; i++)
				{
					if (::GetMenuString(_mainMenuHandle, i, menuLangName, nbChar-1, MF_BYCOMMAND))
						if (!lstrcmp(userLangName, menuLangName))
						{
							::CheckMenuRadioItem(_mainMenuHandle, IDM_LANG_C, IDM_LANG_USER_LIMIT, i, MF_BYCOMMAND);
							return;
						}
				}
			}
		}
	}
	::CheckMenuRadioItem(_mainMenuHandle, IDM_LANG_C, IDM_LANG_USER_LIMIT, id, MF_BYCOMMAND);
}

generic_string Notepad_plus::getLangDesc(LangType langType, bool shortDesc)
{

	if ((langType >= L_EXTERNAL) && (langType < NppParameters::getInstance()->L_END))
	{
		ExternalLangContainer & elc = NppParameters::getInstance()->getELCFromIndex(langType - L_EXTERNAL);
		if (shortDesc)
			return generic_string(elc._name);
		else
			return generic_string(elc._desc);
	}

	if (langType > L_EXTERNAL)
        langType = L_TEXT;

	generic_string str2Show = ScintillaEditView::langNames[langType].longName;

	if (langType == L_USER)
	{
		Buffer * currentBuf = _pEditView->getCurrentBuffer();
		if (currentBuf->isUserDefineLangExt())
		{
			str2Show += TEXT(" - ");
			str2Show += currentBuf->getUserDefineLangName();
		}
	}
	return str2Show;
}

void Notepad_plus::copyMarkedLines() 
{
	int lastLine = _pEditView->lastZeroBasedLineNumber();
	generic_string globalStr = TEXT("");
	for (int i = lastLine ; i >= 0 ; i--)
	{
		if (bookmarkPresent(i))
		{
			generic_string currentStr = getMarkedLine(i) + globalStr;
			globalStr = currentStr;
		}
	}
	str2Cliboard(globalStr.c_str());
}

void Notepad_plus::cutMarkedLines()
{
	int lastLine = _pEditView->lastZeroBasedLineNumber();
	generic_string globalStr = TEXT("");

	_pEditView->execute(SCI_BEGINUNDOACTION);
	for (int i = lastLine ; i >= 0 ; i--)
	{
		if (bookmarkPresent(i))
		{
			generic_string currentStr = getMarkedLine(i) + globalStr;
			globalStr = currentStr;

			deleteMarkedline(i);
		}
	}
	_pEditView->execute(SCI_ENDUNDOACTION);
	str2Cliboard(globalStr.c_str());
}

void Notepad_plus::deleteMarkedLines()
{
	int lastLine = _pEditView->lastZeroBasedLineNumber();

	_pEditView->execute(SCI_BEGINUNDOACTION);
	for (int i = lastLine ; i >= 0 ; i--)
	{
		if (bookmarkPresent(i))
			deleteMarkedline(i);
	}
	_pEditView->execute(SCI_ENDUNDOACTION);
}

void Notepad_plus::pasteToMarkedLines()
{
	int clipFormat;
#ifdef UNICODE
	clipFormat = CF_UNICODETEXT;
#else
	clipFormat = CF_TEXT;
#endif
	BOOL canPaste = ::IsClipboardFormatAvailable(clipFormat);
	if (!canPaste)
		return;
	int lastLine = _pEditView->lastZeroBasedLineNumber();

	::OpenClipboard(_hSelf);
	HANDLE clipboardData = ::GetClipboardData(clipFormat);
	::GlobalSize(clipboardData);
	LPVOID clipboardDataPtr = ::GlobalLock(clipboardData);

	generic_string clipboardStr = (const TCHAR *)clipboardDataPtr;

	::GlobalUnlock(clipboardData);	
	::CloseClipboard();

	_pEditView->execute(SCI_BEGINUNDOACTION);
	for (int i = lastLine ; i >= 0 ; i--)
	{
		if (bookmarkPresent(i))
		{
			replaceMarkedline(i, clipboardStr.c_str());
		}
	}
	_pEditView->execute(SCI_ENDUNDOACTION);
}

void Notepad_plus::deleteMarkedline(int ln) 
{
	int lineLen = _pEditView->execute(SCI_LINELENGTH, ln);
	int lineBegin = _pEditView->execute(SCI_POSITIONFROMLINE, ln);
	
	bookmarkDelete(ln);
	TCHAR emptyString[2] = TEXT("");
	_pEditView->replaceTarget(emptyString, lineBegin, lineBegin + lineLen);
}

void Notepad_plus::replaceMarkedline(int ln, const TCHAR *str) 
{
	int lineBegin = _pEditView->execute(SCI_POSITIONFROMLINE, ln);
	int lineEnd = _pEditView->execute(SCI_GETLINEENDPOSITION, ln);

	_pEditView->replaceTarget(str, lineBegin, lineEnd);
}

generic_string Notepad_plus::getMarkedLine(int ln) 
{
	int lineLen = _pEditView->execute(SCI_LINELENGTH, ln);
	int lineBegin = _pEditView->execute(SCI_POSITIONFROMLINE, ln);

	TCHAR * buf = new TCHAR[lineLen+1];
	_pEditView->getGenericText(buf, lineBegin, lineBegin + lineLen);
	generic_string line = buf;
	delete [] buf;

	return line;
}

void Notepad_plus::findMatchingBracePos(int & braceAtCaret, int & braceOpposite)
{
	int caretPos = int(_pEditView->execute(SCI_GETCURRENTPOS));
	braceAtCaret = -1;
	braceOpposite = -1;
	TCHAR charBefore = '\0';

	int lengthDoc = int(_pEditView->execute(SCI_GETLENGTH));

	if ((lengthDoc > 0) && (caretPos > 0)) 
    {
		charBefore = TCHAR(_pEditView->execute(SCI_GETCHARAT, caretPos - 1, 0));
	}
	// Priority goes to character before caret
	if (charBefore && generic_strchr(TEXT("[](){}"), charBefore))
    {
		braceAtCaret = caretPos - 1;
	}

	if (lengthDoc > 0  && (braceAtCaret < 0)) 
    {
		// No brace found so check other side
		TCHAR charAfter = TCHAR(_pEditView->execute(SCI_GETCHARAT, caretPos, 0));
		if (charAfter && generic_strchr(TEXT("[](){}"), charAfter))
        {
			braceAtCaret = caretPos;
		}
	}
	if (braceAtCaret >= 0) 
		braceOpposite = int(_pEditView->execute(SCI_BRACEMATCH, braceAtCaret, 0));
}

// return true if 1 or 2 (matched) brace(s) is found
bool Notepad_plus::braceMatch() 
{
	int braceAtCaret = -1;
	int braceOpposite = -1;
	findMatchingBracePos(braceAtCaret, braceOpposite);

	if ((braceAtCaret != -1) && (braceOpposite == -1))
    {
		_pEditView->execute(SCI_BRACEBADLIGHT, braceAtCaret);
		_pEditView->execute(SCI_SETHIGHLIGHTGUIDE, 0);
	} 
    else 
    {
		_pEditView->execute(SCI_BRACEHIGHLIGHT, braceAtCaret, braceOpposite);

		if (_pEditView->isShownIndentGuide())
        {
            int columnAtCaret = int(_pEditView->execute(SCI_GETCOLUMN, braceAtCaret));
		    int columnOpposite = int(_pEditView->execute(SCI_GETCOLUMN, braceOpposite));
			_pEditView->execute(SCI_SETHIGHLIGHTGUIDE, (columnAtCaret < columnOpposite)?columnAtCaret:columnOpposite);
        }
    }

    enableCommand(IDM_SEARCH_GOTOMATCHINGBRACE, (braceAtCaret != -1) && (braceOpposite != -1), MENU | TOOLBAR);
    return (braceAtCaret != -1);
}


void Notepad_plus::setDisplayFormat(formatType f)
{
	generic_string str;
	switch (f)
	{
		case MAC_FORMAT :
			str = TEXT("Macintosh");
			break;
		case UNIX_FORMAT :
			str = TEXT("UNIX");
			break;
		default :
			str = TEXT("Dos\\Windows");
	}
	_statusBar.setText(str.c_str(), STATUSBAR_EOF_FORMAT);
}

void Notepad_plus::setUniModeText()
{
	Buffer *buf = _pEditView->getCurrentBuffer();
	int encoding = buf->getEncoding();
	UniMode um = buf->getUnicodeMode();

	generic_string uniModeTextString;

	if (encoding == -1)
	{
		switch (um)
		{
			case uniUTF8:
				uniModeTextString = TEXT("UTF-8"); break;
			case uni16BE:
				uniModeTextString = TEXT("UCS-2 Big Endian"); break;
			case uni16LE:
				uniModeTextString = TEXT("UCS-2 Little Endian"); break;
			case uni16BE_NoBOM:
				uniModeTextString = TEXT("UCS-2 BE w/o BOM"); break;
			case uni16LE_NoBOM:
				uniModeTextString = TEXT("UCS-2 LE w/o BOM"); break;
			case uniCookie:
				uniModeTextString = TEXT("ANSI as UTF-8"); break;
			default :
				uniModeTextString = TEXT("ANSI");
		}
	}
	else
	{
		EncodingMapper *em = EncodingMapper::getInstance();
		int cmdID = em->getIndexFromEncoding(encoding);
		if (cmdID == -1)
		{
			//printStr(TEXT("Encoding problem. Encoding is not added in encoding_table?"));
			return;
		}
		cmdID += IDM_FORMAT_ENCODE;

		const int itemSize = 64;
		TCHAR uniModeText[itemSize];
		::GetMenuString(_mainMenuHandle, cmdID, uniModeText, itemSize, MF_BYCOMMAND);
		uniModeTextString = uniModeText;
	}
	_statusBar.setText(uniModeTextString.c_str(), STATUSBAR_UNICODE_TYPE);
}

int Notepad_plus::getFolderMarginStyle() const 
{
    if (::GetMenuState(_mainMenuHandle, IDM_VIEW_FOLDERMAGIN_SIMPLE, MF_BYCOMMAND) == MF_CHECKED)
        return IDM_VIEW_FOLDERMAGIN_SIMPLE;
    
    if (::GetMenuState(_mainMenuHandle, IDM_VIEW_FOLDERMAGIN_ARROW, MF_BYCOMMAND) == MF_CHECKED)
        return IDM_VIEW_FOLDERMAGIN_ARROW;

    if (::GetMenuState(_mainMenuHandle, IDM_VIEW_FOLDERMAGIN_CIRCLE, MF_BYCOMMAND) == MF_CHECKED)
        return IDM_VIEW_FOLDERMAGIN_CIRCLE;

    if (::GetMenuState(_mainMenuHandle, IDM_VIEW_FOLDERMAGIN_BOX, MF_BYCOMMAND) == MF_CHECKED)
        return IDM_VIEW_FOLDERMAGIN_BOX;

	return 0;
}

int Notepad_plus::getFolderMaginStyleIDFrom(folderStyle fStyle) const
{
    switch (fStyle)
    {
        case FOLDER_STYLE_SIMPLE : return IDM_VIEW_FOLDERMAGIN_SIMPLE;
        case FOLDER_STYLE_ARROW : return IDM_VIEW_FOLDERMAGIN_ARROW;
        case FOLDER_STYLE_CIRCLE : return IDM_VIEW_FOLDERMAGIN_CIRCLE;
        case FOLDER_STYLE_BOX : return IDM_VIEW_FOLDERMAGIN_BOX;
		default : return FOLDER_TYPE;
    }
}


void Notepad_plus::charAdded(TCHAR chAdded)
{
	bool indentMaintain = NppParameters::getInstance()->getNppGUI()._maitainIndent;
	if (indentMaintain)
		MaintainIndentation(chAdded);
}

void Notepad_plus::addHotSpot(bool docIsModifing)
{
	//bool docIsModifing = true;
	int posBegin2style = 0;
	if (docIsModifing)
		posBegin2style = _pEditView->execute(SCI_GETCURRENTPOS);

	int endStyle = _pEditView->execute(SCI_GETENDSTYLED);
	if (docIsModifing)
	{

 		posBegin2style = _pEditView->execute(SCI_GETCURRENTPOS);
		if (posBegin2style > 0) posBegin2style--;
		UCHAR ch = (UCHAR)_pEditView->execute(SCI_GETCHARAT, posBegin2style);

		// determinating the type of EOF to make sure how many steps should we be back
		if ((ch == 0x0A) || (ch == 0x0D))
		{
			int eolMode = _pEditView->execute(SCI_GETEOLMODE);
			
			if ((eolMode == SC_EOL_CRLF) && (posBegin2style > 1))
				posBegin2style -= 2;
			else if (posBegin2style > 0)
				posBegin2style -= 1;
		}

		ch = (UCHAR)_pEditView->execute(SCI_GETCHARAT, posBegin2style);
		while ((posBegin2style > 0) && ((ch != 0x0A) && (ch != 0x0D)))
		{
			ch = (UCHAR)_pEditView->execute(SCI_GETCHARAT, posBegin2style--);
		}
	}
	

	int startPos = 0;
	int endPos = _pEditView->execute(SCI_GETTEXTLENGTH);

	_pEditView->execute(SCI_SETSEARCHFLAGS, SCFIND_REGEXP|SCFIND_POSIX);

	_pEditView->execute(SCI_SETTARGETSTART, startPos);
	_pEditView->execute(SCI_SETTARGETEND, endPos);

	vector<pair<int, int> > hotspotStylers;
	int style_hotspot = 30;
	int posFound = _pEditView->execute(SCI_SEARCHINTARGET, strlen(URL_HTTP_REG_EXPR), (LPARAM)URL_HTTP_REG_EXPR);

	while (posFound != -1)
	{
		int start = int(_pEditView->execute(SCI_GETTARGETSTART));
		int end = int(_pEditView->execute(SCI_GETTARGETEND));
		int foundTextLen = end - start;
		int idStyle = _pEditView->execute(SCI_GETSTYLEAT, posFound);

		if (end < posBegin2style - 1)
		{
			if (style_hotspot > 1)
				style_hotspot--;
		}
		else
		{
			int fs = -1;
			for (size_t i = 0 ; i < hotspotStylers.size() ; i++)
			{
				if (hotspotStylers[i].second == idStyle)
				{
					fs = hotspotStylers[i].first;
					break;
				}
			}

			if (fs != -1)
			{
				_pEditView->execute(SCI_STARTSTYLING, start, 0xFF);
				_pEditView->execute(SCI_SETSTYLING, foundTextLen, fs);

			}
			else
			{
				pair<int, int> p(style_hotspot, idStyle);
				hotspotStylers.push_back(p);
				int activeFG = 0xFF0000;
				char fontNameA[128];

				Style hotspotStyle;
				
				hotspotStyle._styleID = style_hotspot;
				_pEditView->execute(SCI_STYLEGETFONT, idStyle, (LPARAM)fontNameA);
				TCHAR *generic_fontname = new TCHAR[128];
#ifdef UNICODE
				WcharMbcsConvertor *wmc = WcharMbcsConvertor::getInstance();
				const wchar_t * fontNameW = wmc->char2wchar(fontNameA, _nativeLangEncoding);
				lstrcpy(generic_fontname, fontNameW);
#else
				lstrcpy(generic_fontname, fontNameA);
#endif
				hotspotStyle._fontName = generic_fontname;

				hotspotStyle._fgColor = _pEditView->execute(SCI_STYLEGETFORE, idStyle);
				hotspotStyle._bgColor = _pEditView->execute(SCI_STYLEGETBACK, idStyle);
				hotspotStyle._fontSize = _pEditView->execute(SCI_STYLEGETSIZE, idStyle);

				int isBold = _pEditView->execute(SCI_STYLEGETBOLD, idStyle);
				int isItalic = _pEditView->execute(SCI_STYLEGETITALIC, idStyle);
				int isUnderline = _pEditView->execute(SCI_STYLEGETUNDERLINE, idStyle);
				hotspotStyle._fontStyle = (isBold?FONTSTYLE_BOLD:0) | (isItalic?FONTSTYLE_ITALIC:0) | (isUnderline?FONTSTYLE_UNDERLINE:0);

				int urlAction = (NppParameters::getInstance())->getNppGUI()._styleURL;
				if (urlAction == 2)
					hotspotStyle._fontStyle |= FONTSTYLE_UNDERLINE;

				_pEditView->setHotspotStyle(hotspotStyle);

				_pEditView->execute(SCI_STYLESETHOTSPOT, style_hotspot, TRUE);
				_pEditView->execute(SCI_SETHOTSPOTACTIVEFORE, TRUE, activeFG);
				_pEditView->execute(SCI_SETHOTSPOTSINGLELINE, style_hotspot, 0);
				_pEditView->execute(SCI_STARTSTYLING, start, 0x1F);
				_pEditView->execute(SCI_SETSTYLING, foundTextLen, style_hotspot);
				if (style_hotspot > 1)
					style_hotspot--;	
			}
		}

		_pEditView->execute(SCI_SETTARGETSTART, posFound + foundTextLen);
		_pEditView->execute(SCI_SETTARGETEND, endPos);
		
		posFound = _pEditView->execute(SCI_SEARCHINTARGET, strlen(URL_HTTP_REG_EXPR), (LPARAM)URL_HTTP_REG_EXPR);
	}

	_pEditView->execute(SCI_STARTSTYLING, endStyle, 0xFF);
	_pEditView->execute(SCI_SETSTYLING, 0, 0);
}



void Notepad_plus::MaintainIndentation(TCHAR ch)
{
	int eolMode = int(_pEditView->execute(SCI_GETEOLMODE));
	int curLine = int(_pEditView->getCurrentLineNumber());
	int lastLine = curLine - 1;
	int indentAmount = 0;

	if (((eolMode == SC_EOL_CRLF || eolMode == SC_EOL_LF) && ch == '\n') ||
	        (eolMode == SC_EOL_CR && ch == '\r')) 
	{
		while (lastLine >= 0 && _pEditView->getLineLength(lastLine) == 0)
			lastLine--;

		if (lastLine >= 0) {
			indentAmount = _pEditView->getLineIndent(lastLine);
		}
		if (indentAmount > 0) {
			_pEditView->setLineIndent(curLine, indentAmount);
		}
	}
}
void Notepad_plus::specialCmd(int id, int param)
{	
	if ((param != 1) && (param != 2)) return;

	NppParameters *pNppParam = NppParameters::getInstance();
	ScintillaEditView *pEditView = (param == 1)?&_mainEditView:&_subEditView;

	switch (id)
	{
        case IDM_VIEW_LINENUMBER:
        case IDM_VIEW_SYMBOLMARGIN:
		//case IDM_VIEW_DOCCHANGEMARGIN:
        case IDM_VIEW_FOLDERMAGIN:
        {
            int margin;
            if (id == IDM_VIEW_LINENUMBER)
                margin = ScintillaEditView::_SC_MARGE_LINENUMBER;
            else if (id == IDM_VIEW_SYMBOLMARGIN)
                margin = ScintillaEditView::_SC_MARGE_SYBOLE;
			/*
            else if (id == IDM_VIEW_DOCCHANGEMARGIN)
			{
				margin = ScintillaEditView::_SC_MARGE_MODIFMARKER;
			}
			*/
			else
				margin = ScintillaEditView::_SC_MARGE_FOLDER;

            if (pEditView->hasMarginShowed(margin))
                pEditView->showMargin(margin, false);
            else
                pEditView->showMargin(margin);

			break;
        }

        case IDM_VIEW_FOLDERMAGIN_SIMPLE:
        case IDM_VIEW_FOLDERMAGIN_ARROW:
        case IDM_VIEW_FOLDERMAGIN_CIRCLE:
        case IDM_VIEW_FOLDERMAGIN_BOX:
        {
            int checkedID = getFolderMarginStyle();
            if (checkedID == id) return;
            folderStyle fStyle = (id == IDM_VIEW_FOLDERMAGIN_SIMPLE)?FOLDER_STYLE_SIMPLE:\
                ((id == IDM_VIEW_FOLDERMAGIN_ARROW)?FOLDER_STYLE_ARROW:\
                ((id == IDM_VIEW_FOLDERMAGIN_CIRCLE)?FOLDER_STYLE_CIRCLE:FOLDER_STYLE_BOX));
            pEditView->setMakerStyle(fStyle);
            break;
        }
		
		case IDM_VIEW_CURLINE_HILITING:
		{
            COLORREF colour = pNppParam->getCurLineHilitingColour();
			pEditView->setCurrentLineHiLiting(!_pEditView->isCurrentLineHiLiting(), colour);
			break;
		}
		
		case IDM_VIEW_EDGEBACKGROUND:
		case IDM_VIEW_EDGELINE:
		case IDM_VIEW_EDGENONE:
		{
			int mode;
			switch (id)
			{
				case IDM_VIEW_EDGELINE:
				{
					mode = EDGE_LINE;
					break;
				}
				case IDM_VIEW_EDGEBACKGROUND:
				{
					mode = EDGE_BACKGROUND;
					break;
				}
				default :
					mode = EDGE_NONE;
			}
			pEditView->execute(SCI_SETEDGEMODE, mode);
			break;
		}

		case IDM_SETTING_EDGE_SIZE :
		{
			ValueDlg nbColumnEdgeDlg;
			ScintillaViewParams & svp = (ScintillaViewParams &)pNppParam->getSVP(param == 1?SCIV_PRIMARY:SCIV_SECOND);
			nbColumnEdgeDlg.init(_hInst, _preference.getHSelf(), svp._edgeNbColumn, TEXT("Nb of column:"));
			nbColumnEdgeDlg.setNBNumber(3);

			POINT p;
			::GetCursorPos(&p);
			::ScreenToClient(_hParent, &p);
			int size = nbColumnEdgeDlg.doDialog(p, _isRTL);

			if (size != -1)
			{
				svp._edgeNbColumn = size;
				pEditView->execute(SCI_SETEDGECOLUMN, size);
			}
			break;
		}
	}
}


void Notepad_plus::setLanguage(LangType langType) {
	//Add logic to prevent changing a language when a document is shared between two views
	//If so, release one document
	bool reset = false;
	Document prev = 0;
	if (bothActive()) {
		if (_mainEditView.getCurrentBufferID() == _subEditView.getCurrentBufferID()) {
			reset = true;
			_subEditView.saveCurrentPos();
			prev = _subEditView.execute(SCI_GETDOCPOINTER);
			_subEditView.execute(SCI_SETDOCPOINTER, 0, 0);
		}
	}
	if (reset) {
		_mainEditView.getCurrentBuffer()->setLangType(langType);
	} else {
		_pEditView->getCurrentBuffer()->setLangType(langType);
	}

	if (reset) {
		_subEditView.execute(SCI_SETDOCPOINTER, 0, prev);
		_subEditView.restoreCurrentPos();
	}
};

enum LangType Notepad_plus::menuID2LangType(int cmdID)
{
	switch (cmdID)
	{
        case IDM_LANG_C	:
            return L_C;
        case IDM_LANG_CPP :
            return L_CPP;
        case IDM_LANG_JAVA :
            return L_JAVA;
        case IDM_LANG_CS :
            return L_CS;
        case IDM_LANG_HTML :
            return L_HTML;
        case IDM_LANG_XML :
            return L_XML;
        case IDM_LANG_JS :
            return L_JS;
        case IDM_LANG_PHP :
            return L_PHP;
        case IDM_LANG_ASP :
            return L_ASP;
        case IDM_LANG_JSP :
            return L_JSP;
        case IDM_LANG_CSS :
            return L_CSS;
        case IDM_LANG_LUA :
            return L_LUA;
        case IDM_LANG_PERL :
            return L_PERL;
        case IDM_LANG_PYTHON :
            return L_PYTHON;
        case IDM_LANG_PASCAL :
            return L_PASCAL;
        case IDM_LANG_BATCH :
            return L_BATCH;
        case IDM_LANG_OBJC :
            return L_OBJC;
        case IDM_LANG_VB :
            return L_VB;
        case IDM_LANG_SQL :
            return L_SQL;
        case IDM_LANG_ASCII :
            return L_ASCII;
        case IDM_LANG_TEXT :
            return L_TEXT;
        case IDM_LANG_RC :
            return L_RC;
        case IDM_LANG_MAKEFILE :
            return L_MAKEFILE;
        case IDM_LANG_INI :
            return L_INI;
        case IDM_LANG_TEX :
            return L_TEX;
        case IDM_LANG_FORTRAN :
            return L_FORTRAN;
        case IDM_LANG_BASH :
            return L_BASH;
        case IDM_LANG_FLASH :
            return L_FLASH;
		case IDM_LANG_NSIS :
            return L_NSIS;
		case IDM_LANG_TCL :
            return L_TCL;
		case IDM_LANG_LISP :
			return L_LISP;
		case IDM_LANG_SCHEME :
			return L_SCHEME;
		case IDM_LANG_ASM :
            return L_ASM;
		case IDM_LANG_DIFF :
            return L_DIFF;
		case IDM_LANG_PROPS :
            return L_PROPS;
		case IDM_LANG_PS:
            return L_PS;
		case IDM_LANG_RUBY:
            return L_RUBY;
		case IDM_LANG_SMALLTALK:
            return L_SMALLTALK;
		case IDM_LANG_VHDL :
            return L_VHDL;
        case IDM_LANG_KIX :
            return L_KIX;
        case IDM_LANG_CAML :
            return L_CAML;
        case IDM_LANG_ADA :
            return L_ADA;
        case IDM_LANG_VERILOG :
            return L_VERILOG;
		case IDM_LANG_MATLAB :
            return L_MATLAB;
		case IDM_LANG_HASKELL :
            return L_HASKELL;
        case IDM_LANG_AU3 :
            return L_AU3;
		case IDM_LANG_INNO :
            return L_INNO;
		case IDM_LANG_CMAKE :
            return L_CMAKE; 
		case IDM_LANG_YAML :
			return L_YAML;
        case IDM_LANG_COBOL :
            return L_COBOL;
        case IDM_LANG_D :
            return L_D;
        case IDM_LANG_GUI4CLI :
            return L_GUI4CLI;
        case IDM_LANG_POWERSHELL :
            return L_POWERSHELL;
        case IDM_LANG_R :
            return L_R;

		case IDM_LANG_USER :
            return L_USER;
		default: {
			if (cmdID >= IDM_LANG_USER && cmdID <= IDM_LANG_USER_LIMIT) {
				return L_USER;
			}
			break; }
	}
	return L_EXTERNAL;
}


void Notepad_plus::setTitle()
{
	const NppGUI & nppGUI = NppParameters::getInstance()->getNppGUI();
	//Get the buffer
	Buffer * buf = _pEditView->getCurrentBuffer();

	generic_string result = TEXT("");
	if (buf->isDirty())
	{
		result += TEXT("*");
	}

	if (nppGUI._shortTitlebar)
	{
		result += buf->getFileName();
	}
	else
	{
		result += buf->getFullPathName();
	}
	result += TEXT(" - ");
	result += _className;
	//::SetWindowText(_hSelf, title);
	::SendMessage(_hSelf, WM_SETTEXT, 0, (LPARAM)result.c_str());
	
}

void Notepad_plus::activateNextDoc(bool direction) 
{
	int nbDoc = _pDocTab->nbItem();

    int curIndex = _pDocTab->getCurrentTabIndex();
    curIndex += (direction == dirUp)?-1:1;

	if (curIndex >= nbDoc)
	{
		if (viewVisible(otherView()))
			switchEditViewTo(otherView());
		curIndex = 0;
	}
	else if (curIndex < 0)
	{
		if (viewVisible(otherView()))
		{
			switchEditViewTo(otherView());
			nbDoc = _pDocTab->nbItem();
		}
		curIndex = nbDoc - 1;
	}

	BufferID id = _pDocTab->getBufferByIndex(curIndex);
	activateBuffer(id, currentView());
}

void Notepad_plus::activateDoc(int pos)
{
	int nbDoc = _pDocTab->nbItem();
	if (pos == _pDocTab->getCurrentTabIndex())
	{
		Buffer * buf = _pEditView->getCurrentBuffer();
		buf->increaseRecentTag();
		return;
	}

	if (pos >= 0 && pos < nbDoc)
	{
		BufferID id = _pDocTab->getBufferByIndex(pos);
		activateBuffer(id, currentView());
	}
}


static const char utflen[] = {1,1,2,3};

size_t Notepad_plus::getSelectedCharNumber(UniMode u)
{
	size_t result = 0;
	if (u == uniUTF8 || u == uniCookie)
	{
		int numSel = _pEditView->execute(SCI_GETSELECTIONS);
		for (int i=0; i < numSel; i++)
		{
			size_t line1 = _pEditView->execute(SCI_LINEFROMPOSITION, _pEditView->execute(SCI_GETSELECTIONNSTART, i));
			size_t line2 = _pEditView->execute(SCI_LINEFROMPOSITION, _pEditView->execute(SCI_GETSELECTIONNEND, i));
			for (size_t j = line1; j <= line2; j++)
			{
				size_t stpos = _pEditView->execute(SCI_GETLINESELSTARTPOSITION, j);
				if (stpos != INVALID_POSITION)
				{
					size_t endpos = _pEditView->execute(SCI_GETLINESELENDPOSITION, j);
					for (size_t pos = stpos; pos < endpos; pos++)
					{
						unsigned char c = 0xf0 & (unsigned char)_pEditView->execute(SCI_GETCHARAT, pos);
						if (c >= 0xc0) 
							pos += utflen[(c & 0x30) >>  4];
						result++;
					}
				}
			}
		}
	}
	else
	{
		for (int i=0; i < _numSel; i++)
		{
			size_t stpos = _pEditView->execute(SCI_GETSELECTIONNSTART, i);
			size_t endpos = _pEditView->execute(SCI_GETSELECTIONNEND, i);
			result += (endpos - stpos);
			size_t line1 = _pEditView->execute(SCI_LINEFROMPOSITION, stpos);
			size_t line2 = _pEditView->execute(SCI_LINEFROMPOSITION, endpos);
			line2 -= line1;
			if (_pEditView->execute(SCI_GETEOLMODE) == SC_EOL_CRLF) line2 *= 2;
			result -= line2;
		}
		if (u != uni8Bit && u != uni7Bit) result *= 2;
	}
	return result;
}

size_t Notepad_plus::getCurrentDocCharCount(size_t numLines, UniMode u)
{
	if (u != uniUTF8 && u != uniCookie)
	{
		int result = _pEditView->execute(SCI_GETLENGTH);
		size_t lines = numLines==0?0:numLines-1;
		if (_pEditView->execute(SCI_GETEOLMODE) == SC_EOL_CRLF) lines *= 2;
		result -= lines;
		return ((int)result < 0)?0:result;
	}
	else
	{
		size_t result = 0;
		for (size_t line=0; line<numLines; line++)
		{
			size_t endpos = _pEditView->execute(SCI_GETLINEENDPOSITION, line);
			for (size_t pos = _pEditView->execute(SCI_POSITIONFROMLINE, line); pos < endpos; pos++)
			{
				unsigned char c = 0xf0 & (unsigned char)_pEditView->execute(SCI_GETCHARAT, pos);
				if (c >= 0xc0) pos += utflen[(c & 0x30) >>  4];
				result++;
			}
		}
		return result;
	}
}

bool Notepad_plus::isFormatUnicode(UniMode u)
{
	return (u != uni8Bit && u != uni7Bit && u != uniUTF8 && u != uniCookie);
}

int Notepad_plus::getBOMSize(UniMode u)
{
	switch(u)
	{
		case uni16LE:
		case uni16BE:
			return 2;
		case uniUTF8:
			return 3;
		default:
			return 0;
	}
}

int Notepad_plus::getSelectedAreas()
{
	_numSel = _pEditView->execute(SCI_GETSELECTIONS);
	if (_numSel == 1) // either 0 or 1 selection
		return (_pEditView->execute(SCI_GETSELECTIONNSTART, 0) == _pEditView->execute(SCI_GETSELECTIONNEND, 0)) ? 0 : 1;
	return (_pEditView->execute(SCI_SELECTIONISRECTANGLE)) ? 1 : _numSel;
}

size_t Notepad_plus::getSelectedBytes()
{
	size_t result = 0;
	for (int i=0; i<_numSel; i++)
		result += (_pEditView->execute(SCI_GETSELECTIONNEND, i) - _pEditView->execute(SCI_GETSELECTIONNSTART, i));
	return result;
}

void Notepad_plus::updateStatusBar() 
{
	UniMode u = _pEditView->getCurrentBuffer()->getUnicodeMode();
    TCHAR strLnCol[64];

	int areas = getSelectedAreas();
	int sizeofChar = (isFormatUnicode(u)) ? 2 : 1;
	wsprintf(strLnCol, TEXT("Ln : %d    Col : %d    Sel : %d (%d bytes) in %d ranges"),\
        (_pEditView->getCurrentLineNumber() + 1), \
		(_pEditView->getCurrentColumnNumber() + 1),\
		getSelectedCharNumber(u), getSelectedBytes() * sizeofChar,\
		areas);

    _statusBar.setText(strLnCol, STATUSBAR_CUR_POS);

	TCHAR strDonLen[64];
    	size_t numLines = _pEditView->execute(SCI_GETLINECOUNT);
    wsprintf(strDonLen, TEXT("%d chars   %d bytes   %d lines"),\
		getCurrentDocCharCount(numLines, u),\
		_pEditView->execute(SCI_GETLENGTH) * sizeofChar + getBOMSize(u),\
		numLines);
	_statusBar.setText(strDonLen, STATUSBAR_DOC_SIZE);
    _statusBar.setText(_pEditView->execute(SCI_GETOVERTYPE) ? TEXT("OVR") : TEXT("INS"), STATUSBAR_TYPING_MODE);
}


void Notepad_plus::dropFiles(HDROP hdrop) 
{
	if (hdrop)
	{
		// Determinate in which view the file(s) is (are) dropped
		POINT p;
		::DragQueryPoint(hdrop, &p);
		HWND hWin = ::RealChildWindowFromPoint(_hSelf, p);
		if (!hWin) return;

		if ((_mainEditView.getHSelf() == hWin) || (_mainDocTab.getHSelf() == hWin))
			switchEditViewTo(MAIN_VIEW);
		else if ((_subEditView.getHSelf() == hWin) || (_subDocTab.getHSelf() == hWin))
			switchEditViewTo(SUB_VIEW);
		else
		{
			::SendMessage(hWin, WM_DROPFILES, (WPARAM)hdrop, 0);
			return;
		}

		int filesDropped = ::DragQueryFile(hdrop, 0xffffffff, NULL, 0);
		BufferID lastOpened = BUFFER_INVALID;
		for (int i = 0 ; i < filesDropped ; ++i)
		{
			TCHAR pathDropped[MAX_PATH];
			::DragQueryFile(hdrop, i, pathDropped, MAX_PATH);
			BufferID test = doOpen(pathDropped);
			if (test != BUFFER_INVALID)
				lastOpened = test;
            //setLangStatus(_pEditView->getCurrentDocType());
		}
		if (lastOpened != BUFFER_INVALID) {
			switchToFile(lastOpened);
		}
		::DragFinish(hdrop);
		// Put Notepad_plus to forefront
		// May not work for Win2k, but OK for lower versions
		// Note: how to drop a file to an iconic window?
		// Actually, it is the Send To command that generates a drop.
		if (::IsIconic(_hSelf))
		{
			::ShowWindow(_hSelf, SW_RESTORE);
		}
		::SetForegroundWindow(_hSelf);
	}
}

void Notepad_plus::checkModifiedDocument()
{
	//this will trigger buffer updates. If the status changes, Notepad++ will be informed and can do its magic
	MainFileManager->checkFilesystemChanges();
}

void Notepad_plus::getMainClientRect(RECT &rc) const
{
    getClientRect(rc);
	rc.top += _rebarTop.getHeight();
	rc.bottom -= rc.top + _rebarBottom.getHeight() + _statusBar.getHeight();
}

void Notepad_plus::showView(int whichOne) {
	if (viewVisible(whichOne))	//no use making visible view visible
		return;

	if (_mainWindowStatus & WindowUserActive) {
		 _pMainSplitter->setWin0(&_subSplitter);
		 _pMainWindow = _pMainSplitter;
	} else {
		_pMainWindow = &_subSplitter;
	}

	if (whichOne == MAIN_VIEW) {
		_mainEditView.display(true);
		_mainDocTab.display(true);
	} else if (whichOne == SUB_VIEW) {
		_subEditView.display(true);
		_subDocTab.display(true);
	}
	_pMainWindow->display(true);

	_mainWindowStatus |= (whichOne==MAIN_VIEW)?WindowMainActive:WindowSubActive;

	//Send sizing info to make windows fit
	::SendMessage(_hSelf, WM_SIZE, 0, 0);
}

bool Notepad_plus::viewVisible(int whichOne) {
	int viewToCheck = (whichOne == SUB_VIEW?WindowSubActive:WindowMainActive);
	return (_mainWindowStatus & viewToCheck) != 0;
}

void Notepad_plus::hideCurrentView()
{
	hideView(currentView());
}

void Notepad_plus::hideView(int whichOne)
{
	if (!(bothActive()))	//cannot close if not both views visible
		return;

	Window * windowToSet = (whichOne == MAIN_VIEW)?&_subDocTab:&_mainDocTab;
	if (_mainWindowStatus & WindowUserActive)
	{
		_pMainSplitter->setWin0(windowToSet);
	}
	else // otherwise the main window is the spltter container that we just created
		_pMainWindow = windowToSet;
	    
	_subSplitter.display(false);	//hide splitter
	//hide scintilla and doctab
	if (whichOne == MAIN_VIEW) {
		_mainEditView.display(false);
		_mainDocTab.display(false);
	} else if (whichOne == SUB_VIEW) {
		_subEditView.display(false);
		_subDocTab.display(false);
	}

	// resize the main window
	::SendMessage(_hSelf, WM_SIZE, 0, 0);

	switchEditViewTo(otherFromView(whichOne));
	int viewToDisable = (whichOne == SUB_VIEW?WindowSubActive:WindowMainActive);
	_mainWindowStatus &= ~viewToDisable;
}

bool Notepad_plus::loadStyles()
{
	NppParameters *pNppParam = NppParameters::getInstance();
	return pNppParam->reloadStylers();
}

bool Notepad_plus::reloadLang() 
{
	NppParameters *pNppParam = NppParameters::getInstance();

	if (!pNppParam->reloadLang())
	{
		return false;
	}

	TiXmlDocumentA *nativeLangDocRootA = pNppParam->getNativeLangA();
	if (!nativeLangDocRootA)
	{
		return false;
	}
	_nativeLangA =  nativeLangDocRootA->FirstChild("NotepadPlus");
	if (!_nativeLangA)
	{
		return false;
	}
	_nativeLangA = _nativeLangA->FirstChild("Native-Langue");
	if (!_nativeLangA)
	{
		return false;
	}
	TiXmlElementA *element = _nativeLangA->ToElement();
	const char *rtl = element->Attribute("RTL");
	if (rtl)
		_isRTL = (strcmp(rtl, "yes") == 0);

	// get encoding
	TiXmlDeclarationA *declaration =  _nativeLangA->GetDocument()->FirstChild()->ToDeclaration();
	if (declaration)
	{
		const char * encodingStr = declaration->Encoding();
		EncodingMapper *em = EncodingMapper::getInstance();
		_nativeLangEncoding = em->getEncodingFromString(encodingStr);
	}
	
	pNppParam->reloadContextMenuFromXmlTree(_mainMenuHandle);

	generic_string pluginsTrans, windowTrans;
	changeMenuLang(pluginsTrans, windowTrans);

	int indexWindow = ::GetMenuItemCount(_mainMenuHandle) - 3;

	if (_pluginsManager.hasPlugins() && pluginsTrans != TEXT(""))
	{
		::ModifyMenu(_mainMenuHandle, indexWindow - 1, MF_BYPOSITION, 0, pluginsTrans.c_str());
	}
	
	if (windowTrans != TEXT(""))
	{
		::ModifyMenu(_mainMenuHandle, indexWindow, MF_BYPOSITION, 0, windowTrans.c_str());
		windowTrans += TEXT("...");
		::ModifyMenu(_mainMenuHandle, IDM_WINDOW_WINDOWS, MF_BYCOMMAND, IDM_WINDOW_WINDOWS, windowTrans.c_str());
	}
	// Update scintilla context menu strings
	vector<MenuItemUnit> & tmp = pNppParam->getContextMenuItems();
	size_t len = tmp.size();
	TCHAR menuName[64];
	for (size_t i = 0 ; i < len ; i++)
	{
		if (tmp[i]._itemName == TEXT(""))
		{
			::GetMenuString(_mainMenuHandle, tmp[i]._cmdID, menuName, 64, MF_BYCOMMAND);
			tmp[i]._itemName = purgeMenuItemString(menuName);
		}
	}
	
	vector<CommandShortcut> & shortcuts = pNppParam->getUserShortcuts();
	len = shortcuts.size();

	for(size_t i = 0; i < len; i++) 
	{
		CommandShortcut & csc = shortcuts[i];
		::GetMenuString(_mainMenuHandle, csc.getID(), menuName, 64, MF_BYCOMMAND);
		csc.setName(purgeMenuItemString(menuName, true).c_str());
	}
	_accelerator.updateFullMenu();

	_scintaccelerator.updateKeys();


	if (_tabPopupMenu.isCreated())
	{
		changeLangTabContextMenu();
	}
	if (_tabPopupDropMenu.isCreated())
	{
		changeLangTabDrapContextMenu();
	}

	if (_preference.isCreated())
	{
		changePrefereceDlgLang();
	}

	if (_configStyleDlg.isCreated())
	{
		changeConfigLang();
	}

	if (_findReplaceDlg.isCreated())
	{
		changeFindReplaceDlgLang();
	}

	if (_goToLineDlg.isCreated())
	{
		changeDlgLang(_goToLineDlg.getHSelf(), "GoToLine");
	}

	if (_runDlg.isCreated())
	{
		changeDlgLang(_runDlg.getHSelf(), "Run");
	}

	if (_runMacroDlg.isCreated())
	{
		changeDlgLang(_runMacroDlg.getHSelf(), "MultiMacro");
	}

	if (_goToLineDlg.isCreated())
	{
		changeDlgLang(_goToLineDlg.getHSelf(), "GoToLine");
	}

	if (_colEditorDlg.isCreated())
	{
		changeDlgLang(_colEditorDlg.getHSelf(), "ColumnEditor");
	}

	UserDefineDialog *udd = _pEditView->getUserDefineDlg();
	if (udd->isCreated())
	{
		changeUserDefineLang();
	}

	_lastRecentFileList.setLangEncoding(_nativeLangEncoding);
	return true;
}

bool Notepad_plus::canHideView(int whichOne)
{
	if (!viewVisible(whichOne))
		return false;	//cannot hide hidden view
	if (!bothActive())
		return false;	//cannot hide only window
	DocTabView * tabToCheck = (whichOne == MAIN_VIEW)?&_mainDocTab:&_subDocTab;
	Buffer * buf = MainFileManager->getBufferByID(tabToCheck->getBufferByIndex(0));
	bool canHide = ((tabToCheck->nbItem() == 1) && !buf->isDirty() && buf->isUntitled());
	return canHide;
}

void Notepad_plus::loadBufferIntoView(BufferID id, int whichOne, bool dontClose) {
	DocTabView * tabToOpen = (whichOne == MAIN_VIEW)?&_mainDocTab:&_subDocTab;
	ScintillaEditView * viewToOpen = (whichOne == MAIN_VIEW)?&_mainEditView:&_subEditView;

	//check if buffer exists
	int index = tabToOpen->getIndexByBuffer(id);
	if (index != -1)	//already open, done
		return;

	BufferID idToClose = BUFFER_INVALID;
	//Check if the tab has a single clean buffer. Close it if so
	if (!dontClose && tabToOpen->nbItem() == 1) {
		idToClose = tabToOpen->getBufferByIndex(0);
		Buffer * buf = MainFileManager->getBufferByID(idToClose);
		if (buf->isDirty() || !buf->isUntitled()) {
			idToClose = BUFFER_INVALID;
		}
	}

	MainFileManager->addBufferReference(id, viewToOpen);

	if (idToClose != BUFFER_INVALID) {	//close clean doc. Use special logic to prevent flicker of tab showing then hiding
		tabToOpen->setBuffer(0, id);	//index 0 since only one open
		activateBuffer(id, whichOne);	//activate. DocTab already activated but not a problem
		MainFileManager->closeBuffer(idToClose, viewToOpen);	//delete the buffer
	} else {
		tabToOpen->addBuffer(id);
	}
}

void Notepad_plus::removeBufferFromView(BufferID id, int whichOne) {
	DocTabView * tabToClose = (whichOne == MAIN_VIEW)?&_mainDocTab:&_subDocTab;
	ScintillaEditView * viewToClose = (whichOne == MAIN_VIEW)?&_mainEditView:&_subEditView;

	//check if buffer exists
	int index = tabToClose->getIndexByBuffer(id);
	if (index == -1)	//doesnt exist, done
		return;
	
	Buffer * buf = MainFileManager->getBufferByID(id);
	
	//Cannot close doc if last and clean
	if (tabToClose->nbItem() == 1) {
		if (!buf->isDirty() && buf->isUntitled()) {
			return;	//done
		}
	}

	int active = tabToClose->getCurrentTabIndex();
	if (active == index) {	//need an alternative (close real doc, put empty one back
		if (tabToClose->nbItem() == 1) {	//need alternative doc, add new one. Use special logic to prevent flicker of adding new tab then closing other	
			BufferID newID = MainFileManager->newEmptyDocument();
			MainFileManager->addBufferReference(newID, viewToClose);
			tabToClose->setBuffer(0, newID);	//can safely use id 0, last (only) tab open
			activateBuffer(newID, whichOne);	//activate. DocTab already activated but not a problem
		} else {
			int toActivate = 0;
			//activate next doc, otherwise prev if not possible
			if (active == tabToClose->nbItem() - 1) {	//prev
				toActivate = active - 1;
			} else {
				toActivate = active;	//activate the 'active' index. Since we remove the tab first, the indices shift (on the right side)
			}
			tabToClose->deletItemAt((size_t)index);	//delete first
			activateBuffer(tabToClose->getBufferByIndex(toActivate), whichOne);	//then activate. The prevent jumpy tab behaviour
		}
	} else {
		tabToClose->deletItemAt((size_t)index);
	}

	MainFileManager->closeBuffer(id, viewToClose);
}

int Notepad_plus::switchEditViewTo(int gid) 
{
	if (currentView() == gid) {	//make sure focus is ok, then leave
		_pEditView->getFocus();	//set the focus
		return gid;
	}
	if (!viewVisible(gid))
		return currentView();	//cannot activate invisible view
	int oldView = currentView();
	int newView = otherView();

	_activeView = newView;
	//Good old switcheroo
	DocTabView * tempTab = _pDocTab;
	_pDocTab = _pNonDocTab;
	_pNonDocTab = tempTab;
	ScintillaEditView * tempView = _pEditView;
	_pEditView = _pNonEditView;
	_pNonEditView = tempView;

	_pEditView->beSwitched();
    _pEditView->getFocus();	//set the focus

	notifyBufferActivated(_pEditView->getCurrentBufferID(), currentView());
	return oldView;
}

void Notepad_plus::dockUserDlg()
{
    if (!_pMainSplitter)
    {
        _pMainSplitter = new SplitterContainer;
        _pMainSplitter->init(_hInst, _hSelf);

        Window *pWindow;
		if (_mainWindowStatus & (WindowMainActive | WindowSubActive))
            pWindow = &_subSplitter;
        else
            pWindow = _pDocTab;

        _pMainSplitter->create(pWindow, ScintillaEditView::getUserDefineDlg(), 8, RIGHT_FIX, 45);
    }

    if (bothActive())
        _pMainSplitter->setWin0(&_subSplitter);
    else 
        _pMainSplitter->setWin0(_pDocTab);

    _pMainSplitter->display();

    _mainWindowStatus |= WindowUserActive;
    _pMainWindow = _pMainSplitter;

	::SendMessage(_hSelf, WM_SIZE, 0, 0);
}

void Notepad_plus::undockUserDlg()
{
    // a cause de surchargement de "display"
    ::ShowWindow(_pMainSplitter->getHSelf(), SW_HIDE);

    if (bothActive())
        _pMainWindow = &_subSplitter;
    else
        _pMainWindow = _pDocTab;
    
    ::SendMessage(_hSelf, WM_SIZE, 0, 0);

    _mainWindowStatus &= ~WindowUserActive;
    (ScintillaEditView::getUserDefineDlg())->display(); 
}

void Notepad_plus::docOpenInNewInstance(FileTransferMode mode, int x, int y)
{
	BufferID bufferID = _pEditView->getCurrentBufferID();
	Buffer * buf = MainFileManager->getBufferByID(bufferID);
	if (buf->isUntitled() || buf->isDirty())
		return;

	TCHAR nppName[MAX_PATH];
	::GetModuleFileName(NULL, nppName, MAX_PATH);
	generic_string command = TEXT("\"");
	command += nppName;
	command += TEXT("\"");

	command += TEXT(" \"$(FULL_CURRENT_PATH)\" -multiInst -nosession -x");
	TCHAR pX[10], pY[10];
	generic_itoa(x, pX, 10);
	generic_itoa(y, pY, 10);
	
	command += pX;
	command += TEXT(" -y");
	command += pY;

	Command cmd(command);
	cmd.run(_hSelf);
	if (mode == TransferMove)
	{
		doClose(bufferID, currentView());
		if (noOpenedDoc())
			::SendMessage(_hSelf, WM_CLOSE, 0, 0);
	}
}

void Notepad_plus::docGotoAnotherEditView(FileTransferMode mode)
{
	// Test if it's only doc to transfer on the hidden view
	// If so then do nothing
	if (mode == TransferMove)
	{
		if (_pDocTab->nbItem() == 1)
		{
			ScintillaEditView *pOtherView = NULL;
			if (_pEditView == &_mainEditView)
			{
				pOtherView = &_subEditView;
			}
			else if (_pEditView == &_subEditView)
			{
				pOtherView = &_mainEditView;
			}
			else
				return;

			if (!pOtherView->isVisible())
				return;
		}
	}

	//First put the doc in the other view if not present (if it is, activate it).
	//Then if needed close in the original tab
	BufferID current = _pEditView->getCurrentBufferID();
	int viewToGo = otherView();
	int indexFound = _pNonDocTab->getIndexByBuffer(current);
	if (indexFound != -1)	//activate it
	{
		activateBuffer(current, otherView());
	}
	else	//open the document, also copying the position
	{
		loadBufferIntoView(current, viewToGo);
		Buffer * buf = MainFileManager->getBufferByID(current);
		_pEditView->saveCurrentPos();	//allow copying of position
		buf->setPosition(buf->getPosition(_pEditView), _pNonEditView);
		_pNonEditView->restoreCurrentPos();	//set position
		activateBuffer(current, otherView());
	}

	//Open the view if it was hidden
	int viewToOpen = (viewToGo == SUB_VIEW?WindowSubActive:WindowMainActive);
	if (!(_mainWindowStatus & viewToOpen)) {
		showView(viewToGo);
	}

	//Close the document if we transfered the document instead of cloning it
	if (mode == TransferMove) 
	{
		//just close the activate document, since thats the one we moved (no search)
		doClose(_pEditView->getCurrentBufferID(), currentView());
		if (noOpenedDoc())
			::SendMessage(_hSelf, WM_CLOSE, 0, 0);
	} // else it was cone, so leave it

	//Activate the other view since thats where the document went
	switchEditViewTo(viewToGo);

	//_linkTriggered = true;
}

bool Notepad_plus::activateBuffer(BufferID id, int whichOne)
{
	//scnN.nmhdr.code = NPPN_DOCSWITCHINGOFF;		//superseeded by NPPN_BUFFERACTIVATED

	Buffer * pBuf = MainFileManager->getBufferByID(id);
	bool reload = pBuf->getNeedReload();
	if (reload)
	{
		MainFileManager->reloadBuffer(id);
		pBuf->setNeedReload(false);
	}
	if (whichOne == MAIN_VIEW)
	{
		if (_mainDocTab.activateBuffer(id))	//only activate if possible
			_mainEditView.activateBuffer(id);
		else
			return false;
	}
	else
	{
		if (_subDocTab.activateBuffer(id))
			_subEditView.activateBuffer(id);
		else
			return false;
	}

	if (reload) 
	{
		performPostReload(whichOne);
	}
	notifyBufferActivated(id, whichOne);

	//scnN.nmhdr.code = NPPN_DOCSWITCHINGIN;		//superseeded by NPPN_BUFFERACTIVATED
	return true;
}

void Notepad_plus::performPostReload(int whichOne) {
	NppParameters *pNppParam = NppParameters::getInstance();
	const NppGUI & nppGUI = pNppParam->getNppGUI();
	bool toEnd = (nppGUI._fileAutoDetection == cdAutoUpdateGo2end) || (nppGUI._fileAutoDetection == cdGo2end);
	if (!toEnd)
		return;
	if (whichOne == MAIN_VIEW) {
		_mainEditView.execute(SCI_GOTOLINE, _mainEditView.execute(SCI_GETLINECOUNT) -1);
	} else {
		_subEditView.execute(SCI_GOTOLINE, _subEditView.execute(SCI_GETLINECOUNT) -1);
	}	
}

void Notepad_plus::bookmarkNext(bool forwardScan) 
{
	int lineno = _pEditView->getCurrentLineNumber();
	int sci_marker = SCI_MARKERNEXT;
	int lineStart = lineno + 1;	//Scan starting from next line
	int lineRetry = 0;				//If not found, try from the beginning
	if (!forwardScan) 
    {
		lineStart = lineno - 1;		//Scan starting from previous line
		lineRetry = int(_pEditView->execute(SCI_GETLINECOUNT));	//If not found, try from the end
		sci_marker = SCI_MARKERPREVIOUS;
	}
	int nextLine = int(_pEditView->execute(sci_marker, lineStart, 1 << MARK_BOOKMARK));
	if (nextLine < 0)
		nextLine = int(_pEditView->execute(sci_marker, lineRetry, 1 << MARK_BOOKMARK));

	if (nextLine < 0)
		return;

    _pEditView->execute(SCI_ENSUREVISIBLEENFORCEPOLICY, nextLine);
	_pEditView->execute(SCI_GOTOLINE, nextLine);
}

void Notepad_plus::dynamicCheckMenuAndTB() const 
{
	// Visibility of 3 margins
    checkMenuItem(IDM_VIEW_LINENUMBER, _pEditView->hasMarginShowed(ScintillaEditView::_SC_MARGE_LINENUMBER));
    checkMenuItem(IDM_VIEW_SYMBOLMARGIN, _pEditView->hasMarginShowed(ScintillaEditView::_SC_MARGE_SYBOLE));
    checkMenuItem(IDM_VIEW_FOLDERMAGIN, _pEditView->hasMarginShowed(ScintillaEditView::_SC_MARGE_FOLDER));

	// Folder margin style
	checkFolderMarginStyleMenu(getFolderMaginStyleIDFrom(_pEditView->getFolderStyle()));

	// Visibility of invisible characters
	bool wsTabShow = _pEditView->isInvisibleCharsShown();
	bool eolShow = _pEditView->isEolVisible();

	bool onlyWS = false;
	bool onlyEOL = false;
	bool bothWSEOL = false;
	if (wsTabShow)
	{
		if (eolShow)
		{
			bothWSEOL = true;
		}
		else
		{
			onlyWS = true;
		}
	}
	else if (eolShow)
	{
		onlyEOL = true;
	}
	
	checkMenuItem(IDM_VIEW_TAB_SPACE, onlyWS);
	checkMenuItem(IDM_VIEW_EOL, onlyEOL);
	checkMenuItem(IDM_VIEW_ALL_CHARACTERS, bothWSEOL);
	_toolBar.setCheck(IDM_VIEW_ALL_CHARACTERS, bothWSEOL);

	// Visibility of the indentation guide line 
	bool b = _pEditView->isShownIndentGuide();
	checkMenuItem(IDM_VIEW_INDENT_GUIDE, b);
	_toolBar.setCheck(IDM_VIEW_INDENT_GUIDE, b);

	// Edge Line
	int mode = int(_pEditView->execute(SCI_GETEDGEMODE));
	checkMenuItem(IDM_VIEW_EDGEBACKGROUND, (MF_BYCOMMAND | ((mode == EDGE_NONE)||(mode == EDGE_LINE))?MF_UNCHECKED:MF_CHECKED) != 0);
	checkMenuItem(IDM_VIEW_EDGELINE, (MF_BYCOMMAND | ((mode == EDGE_NONE)||(mode == EDGE_BACKGROUND))?MF_UNCHECKED:MF_CHECKED) != 0);

	// Current Line Highlighting
	checkMenuItem(IDM_VIEW_CURLINE_HILITING, _pEditView->isCurrentLineHiLiting());

	// Wrap
	b = _pEditView->isWrap();
	checkMenuItem(IDM_VIEW_WRAP, b);
	_toolBar.setCheck(IDM_VIEW_WRAP, b);
	checkMenuItem(IDM_VIEW_WRAP_SYMBOL, _pEditView->isWrapSymbolVisible());

	//Format conversion
	enableConvertMenuItems(_pEditView->getCurrentBuffer()->getFormat());
	checkUnicodeMenuItems(/*_pEditView->getCurrentBuffer()->getUnicodeMode()*/);

	//Syncronized scrolling 
}

void Notepad_plus::enableConvertMenuItems(formatType f) const 
{
	enableCommand(IDM_FORMAT_TODOS, (f != WIN_FORMAT), MENU);
	enableCommand(IDM_FORMAT_TOUNIX, (f != UNIX_FORMAT), MENU);
	enableCommand(IDM_FORMAT_TOMAC, (f != MAC_FORMAT), MENU);
}

void Notepad_plus::checkUnicodeMenuItems() const 
{
	Buffer *buf = _pEditView->getCurrentBuffer();
	UniMode um = buf->getUnicodeMode();
	int encoding = buf->getEncoding();

	int id = -1;
	switch (um)
	{
		case uniUTF8   : id = IDM_FORMAT_UTF_8; break;
		case uni16BE   : id = IDM_FORMAT_UCS_2BE; break;
		case uni16LE   : id = IDM_FORMAT_UCS_2LE; break;
		case uniCookie : id = IDM_FORMAT_AS_UTF_8; break;
		case uni8Bit   : id = IDM_FORMAT_ANSI; break;
	}

	if (encoding == -1)
	{
		// Uncheck all in the sub encoding menu
		::CheckMenuRadioItem(_mainMenuHandle, IDM_FORMAT_ENCODE, IDM_FORMAT_ENCODE_END, IDM_FORMAT_ENCODE, MF_BYCOMMAND);
		::CheckMenuItem(_mainMenuHandle, IDM_FORMAT_ENCODE, MF_UNCHECKED | MF_BYCOMMAND);

		if (id == -1) //um == uni16BE_NoBOM || um == uni16LE_NoBOM
		{
			// Uncheck all in the main encoding menu
			::CheckMenuRadioItem(_mainMenuHandle, IDM_FORMAT_ANSI, IDM_FORMAT_AS_UTF_8, IDM_FORMAT_ANSI, MF_BYCOMMAND);
			::CheckMenuItem(_mainMenuHandle, IDM_FORMAT_ANSI, MF_UNCHECKED | MF_BYCOMMAND);
		}
		else
		{
			::CheckMenuRadioItem(_mainMenuHandle, IDM_FORMAT_ANSI, IDM_FORMAT_AS_UTF_8, id, MF_BYCOMMAND);
		}
	}
	else
	{
		EncodingMapper *em = EncodingMapper::getInstance();
		int cmdID = em->getIndexFromEncoding(encoding);
		if (cmdID == -1)
		{
			//printStr(TEXT("Encoding problem. Encoding is not added in encoding_table?"));
			return;
		}
		cmdID += IDM_FORMAT_ENCODE;

		// Uncheck all in the main encoding menu
		::CheckMenuRadioItem(_mainMenuHandle, IDM_FORMAT_ANSI, IDM_FORMAT_AS_UTF_8, IDM_FORMAT_ANSI, MF_BYCOMMAND);
		::CheckMenuItem(_mainMenuHandle, IDM_FORMAT_ANSI, MF_UNCHECKED | MF_BYCOMMAND);
		
		// Check the encoding item
		::CheckMenuRadioItem(_mainMenuHandle, IDM_FORMAT_ENCODE, IDM_FORMAT_ENCODE_END, cmdID, MF_BYCOMMAND);
	}
}

void Notepad_plus::showAutoComp() {
	bool isFromPrimary = _pEditView == &_mainEditView;
	AutoCompletion * autoC = isFromPrimary?&_autoCompleteMain:&_autoCompleteSub;
	autoC->showAutoComplete();
}

void Notepad_plus::autoCompFromCurrentFile(bool autoInsert) {
	bool isFromPrimary = _pEditView == &_mainEditView;
	AutoCompletion * autoC = isFromPrimary?&_autoCompleteMain:&_autoCompleteSub;
	autoC->showWordComplete(autoInsert);
}

void Notepad_plus::showFunctionComp() {
	bool isFromPrimary = _pEditView == &_mainEditView;
	AutoCompletion * autoC = isFromPrimary?&_autoCompleteMain:&_autoCompleteSub;
	autoC->showFunctionComplete();
}

void Notepad_plus::changeMenuLang(generic_string & pluginsTrans, generic_string & windowTrans)
{
	if (!_nativeLangA) return;
	TiXmlNodeA *mainMenu = _nativeLangA->FirstChild("Menu");
	if (!mainMenu) return;
	mainMenu = mainMenu->FirstChild("Main");
	if (!mainMenu) return;
	TiXmlNodeA *entriesRoot = mainMenu->FirstChild("Entries");
	if (!entriesRoot) return;
	const char *idName = NULL;

#ifdef UNICODE
	WcharMbcsConvertor *wmc = WcharMbcsConvertor::getInstance();
#endif

	for (TiXmlNodeA *childNode = entriesRoot->FirstChildElement("Item");
		childNode ;
		childNode = childNode->NextSibling("Item") )
	{
		TiXmlElementA *element = childNode->ToElement();
		int id;
		if (element->Attribute("id", &id))
		{
			const char *name = element->Attribute("name");

#ifdef UNICODE
			const wchar_t *nameW = wmc->char2wchar(name, _nativeLangEncoding);
			::ModifyMenu(_mainMenuHandle, id, MF_BYPOSITION, 0, nameW);
#else
			::ModifyMenu(_mainMenuHandle, id, MF_BYPOSITION, 0, name);
#endif
		}
		else 
		{
			idName = element->Attribute("idName");
			if (idName)
			{
				const char *name = element->Attribute("name");
				if (!strcmp(idName, "Plugins"))
				{
#ifdef UNICODE
					const wchar_t *nameW = wmc->char2wchar(name, _nativeLangEncoding);
					pluginsTrans = nameW;
#else
					pluginsTrans = name;
#endif
				}
				else if (!strcmp(idName, "Window"))
				{
#ifdef UNICODE
					const wchar_t *nameW = wmc->char2wchar(name, _nativeLangEncoding);
					windowTrans = nameW;
#else
					windowTrans = name;
	#endif
				}
			}
	}
	}

	TiXmlNodeA *menuCommandsRoot = mainMenu->FirstChild("Commands");
	for (TiXmlNodeA *childNode = menuCommandsRoot->FirstChildElement("Item");
		childNode ;
		childNode = childNode->NextSibling("Item") )
	{
		TiXmlElementA *element = childNode->ToElement();
		int id;
		element->Attribute("id", &id);
		const char *name = element->Attribute("name");

#ifdef UNICODE
		const wchar_t *nameW = wmc->char2wchar(name, _nativeLangEncoding);
		::ModifyMenu(_mainMenuHandle, id, MF_BYCOMMAND, id, nameW);
#else
		::ModifyMenu(_mainMenuHandle, id, MF_BYCOMMAND, id, name);
#endif
	}

	TiXmlNodeA *subEntriesRoot = mainMenu->FirstChild("SubEntries");

	for (TiXmlNodeA *childNode = subEntriesRoot->FirstChildElement("Item");
		childNode ;
		childNode = childNode->NextSibling("Item") )
	{
		TiXmlElementA *element = childNode->ToElement();
		int x, y, z;
		const char *xStr = element->Attribute("posX", &x);
		const char *yStr = element->Attribute("posY", &y);
		const char *name = element->Attribute("name");
		if (!xStr || !yStr || !name)
			continue;

		HMENU hSubMenu = ::GetSubMenu(_mainMenuHandle, x);
		if (!hSubMenu)
			continue;
		HMENU hSubMenu2 = ::GetSubMenu(hSubMenu, y);
		if (!hSubMenu2)
			continue;

		HMENU hMenu = hSubMenu;
		int pos = y;

		const char *zStr = element->Attribute("posZ", &z);
		if (zStr)
		{
			HMENU hSubMenu3 = ::GetSubMenu(hSubMenu2, z);
			if (!hSubMenu3)
				continue;
			hMenu = hSubMenu2;
			pos = z;
		}
#ifdef UNICODE

		const wchar_t *nameW = wmc->char2wchar(name, _nativeLangEncoding);
		::ModifyMenu(hMenu, pos, MF_BYPOSITION, 0, nameW);
#else
		::ModifyMenu(hMenu, pos, MF_BYPOSITION, 0, name);
#endif
	}
	::DrawMenuBar(_hSelf);
}

void Notepad_plus::changeLangTabContextMenu()
{
	const int POS_CLOSE = 0;
	const int POS_CLOSEBUT = 1;
	const int POS_SAVE = 2;
	const int POS_SAVEAS = 3;
	const int POS_RENAME = 4;
	const int POS_REMOVE = 5;
	const int POS_PRINT = 6;
	//------7
	const int POS_READONLY = 8;
	const int POS_CLEARREADONLY = 9;
	//------10
	const int POS_CLIPFULLPATH = 11;
	const int POS_CLIPFILENAME = 12;
	const int POS_CLIPCURRENTDIR = 13;
	//------14
	const int POS_GO2VIEW = 15;
	const int POS_CLONE2VIEW = 16;
	const int POS_GO2NEWINST = 17;
	const int POS_OPENINNEWINST = 18;

	const char *pClose = NULL;
	const char *pCloseBut = NULL;
	const char *pSave = NULL;
	const char *pSaveAs = NULL;
	const char *pPrint = NULL;
	const char *pReadOnly = NULL;
	const char *pClearReadOnly = NULL;
	const char *pGoToView = NULL;
	const char *pCloneToView = NULL;
	const char *pGoToNewInst = NULL;
	const char *pOpenInNewInst = NULL;
	const char *pCilpFullPath = NULL;
	const char *pCilpFileName = NULL;
	const char *pCilpCurrentDir = NULL;
	const char *pRename = NULL;
	const char *pRemove = NULL;
	if (_nativeLangA)
	{
		TiXmlNodeA *tabBarMenu = _nativeLangA->FirstChild("Menu");
		if (tabBarMenu) 
		{
			tabBarMenu = tabBarMenu->FirstChild("TabBar");
			if (tabBarMenu)
			{
				for (TiXmlNodeA *childNode = tabBarMenu->FirstChildElement("Item");
					childNode ;
					childNode = childNode->NextSibling("Item") )
				{
					TiXmlElementA *element = childNode->ToElement();
					int ordre;
					element->Attribute("order", &ordre);
					switch (ordre)
					{
						case 0 :
							pClose = element->Attribute("name"); break;
						case 1 :
							pCloseBut = element->Attribute("name"); break;
						case 2 :
							pSave = element->Attribute("name"); break;
						case 3 :
							pSaveAs = element->Attribute("name"); break;
						case 4 :
							pPrint = element->Attribute("name"); break;
						case 5 :
							pGoToView = element->Attribute("name"); break;
						case 6 :
							pCloneToView = element->Attribute("name"); break;
						case 7 :
							pCilpFullPath = element->Attribute("name"); break;
						case 8 :
							pCilpFileName = element->Attribute("name"); break;
						case 9 :
							pCilpCurrentDir = element->Attribute("name"); break;
						case 10 :
							pRename = element->Attribute("name"); break;
						case 11 :
							pRemove = element->Attribute("name"); break;
						case 12 :
							pReadOnly = element->Attribute("name"); break;
						case 13 :
							pClearReadOnly = element->Attribute("name"); break;
						case 14 :
							pGoToNewInst = element->Attribute("name"); break;
						case 15 :
							pOpenInNewInst = element->Attribute("name"); break;
					}
				}
			}	
		}
	}
	HMENU hCM = _tabPopupMenu.getMenuHandle();
	
#ifdef UNICODE
	WcharMbcsConvertor *wmc = WcharMbcsConvertor::getInstance();
	if (pGoToView && pGoToView[0])
	{
		const wchar_t *goToViewG = wmc->char2wchar(pGoToView, _nativeLangEncoding);
		int cmdID = ::GetMenuItemID(hCM, POS_GO2VIEW);
		::ModifyMenu(hCM, POS_GO2VIEW, MF_BYPOSITION, cmdID, goToViewG);
	}
	if (pCloneToView && pCloneToView[0])
	{
		const wchar_t *cloneToViewG = wmc->char2wchar(pCloneToView, _nativeLangEncoding);
		int cmdID = ::GetMenuItemID(hCM, POS_CLONE2VIEW);
		::ModifyMenu(hCM, POS_CLONE2VIEW, MF_BYPOSITION, cmdID, cloneToViewG);
	}
	if (pGoToNewInst && pGoToNewInst[0])
	{
		const wchar_t *goToNewInstG = wmc->char2wchar(pGoToNewInst, _nativeLangEncoding);
		int cmdID = ::GetMenuItemID(hCM, POS_GO2NEWINST);
		::ModifyMenu(hCM, POS_GO2NEWINST, MF_BYPOSITION, cmdID, goToNewInstG);
	}
	if (pOpenInNewInst && pOpenInNewInst[0])
	{
		const wchar_t *openInNewInstG = wmc->char2wchar(pOpenInNewInst, _nativeLangEncoding);
		int cmdID = ::GetMenuItemID(hCM, POS_OPENINNEWINST);
		::ModifyMenu(hCM, POS_OPENINNEWINST, MF_BYPOSITION, cmdID, openInNewInstG);
	}
	if (pClose && pClose[0])
	{
		const wchar_t *closeG = wmc->char2wchar(pClose, _nativeLangEncoding);
		int cmdID = ::GetMenuItemID(hCM, POS_CLOSE);
		::ModifyMenu(hCM, POS_CLOSE, MF_BYPOSITION, cmdID, closeG);
	}
	if (pCloseBut && pCloseBut[0])
	{
		const wchar_t *closeButG = wmc->char2wchar(pCloseBut, _nativeLangEncoding);
		int cmdID = ::GetMenuItemID(hCM, POS_CLOSEBUT);
		::ModifyMenu(hCM, POS_CLOSEBUT, MF_BYPOSITION, cmdID, closeButG);
	}
	if (pSave && pSave[0])
	{
		const wchar_t *saveG = wmc->char2wchar(pSave, _nativeLangEncoding);
		int cmdID = ::GetMenuItemID(hCM, POS_SAVE);
		::ModifyMenu(hCM, POS_SAVE, MF_BYPOSITION, cmdID, saveG);
	}
	if (pSaveAs && pSaveAs[0])
	{
		const wchar_t *saveAsG = wmc->char2wchar(pSaveAs, _nativeLangEncoding);
		int cmdID = ::GetMenuItemID(hCM, POS_SAVEAS);
		::ModifyMenu(hCM, POS_SAVEAS, MF_BYPOSITION, cmdID, saveAsG);
	}
	if (pPrint && pPrint[0])
	{
		const wchar_t *printG = wmc->char2wchar(pPrint, _nativeLangEncoding);
		int cmdID = ::GetMenuItemID(hCM, POS_PRINT);
		::ModifyMenu(hCM, POS_PRINT, MF_BYPOSITION, cmdID, printG);
	}
	if (pReadOnly && pReadOnly[0])
	{
		const wchar_t *readOnlyG = wmc->char2wchar(pReadOnly, _nativeLangEncoding);
		int cmdID = ::GetMenuItemID(hCM, POS_READONLY);
		::ModifyMenu(hCM, POS_READONLY, MF_BYPOSITION, cmdID, readOnlyG);
	}
	if (pClearReadOnly && pClearReadOnly[0])
	{
		const wchar_t *clearReadOnlyG = wmc->char2wchar(pClearReadOnly, _nativeLangEncoding);
		int cmdID = ::GetMenuItemID(hCM, POS_CLEARREADONLY);
		::ModifyMenu(hCM, POS_CLEARREADONLY, MF_BYPOSITION, cmdID, clearReadOnlyG);
	}
	if (pCilpFullPath && pCilpFullPath[0])
	{
		const wchar_t *cilpFullPathG = wmc->char2wchar(pCilpFullPath, _nativeLangEncoding);
		int cmdID = ::GetMenuItemID(hCM, POS_CLIPFULLPATH);
		::ModifyMenu(hCM, POS_CLIPFULLPATH, MF_BYPOSITION, cmdID, cilpFullPathG);
	}
	if (pCilpFileName && pCilpFileName[0])
	{
		const wchar_t *cilpFileNameG = wmc->char2wchar(pCilpFileName, _nativeLangEncoding);
		int cmdID = ::GetMenuItemID(hCM, POS_CLIPFILENAME);
		::ModifyMenu(hCM, POS_CLIPFILENAME, MF_BYPOSITION, cmdID, cilpFileNameG);
	}
	if (pCilpCurrentDir && pCilpCurrentDir[0])
	{
		const wchar_t * cilpCurrentDirG= wmc->char2wchar(pCilpCurrentDir, _nativeLangEncoding);
		int cmdID = ::GetMenuItemID(hCM, POS_CLIPCURRENTDIR);
		::ModifyMenu(hCM, POS_CLIPCURRENTDIR, MF_BYPOSITION, cmdID, cilpCurrentDirG);
	}
	if (pRename && pRename[0])
	{
		const wchar_t *renameG = wmc->char2wchar(pRename, _nativeLangEncoding);
		int cmdID = ::GetMenuItemID(hCM, POS_RENAME);
		::ModifyMenu(hCM, POS_RENAME, MF_BYPOSITION, cmdID, renameG);
	}
	if (pRemove && pRemove[0])
	{
		const wchar_t *removeG = wmc->char2wchar(pRemove, _nativeLangEncoding);
		int cmdID = ::GetMenuItemID(hCM, POS_REMOVE);
		::ModifyMenu(hCM, POS_REMOVE, MF_BYPOSITION, cmdID, removeG);
	}
#else
	if (pGoToView && pGoToView[0])
	{
		int cmdID = ::GetMenuItemID(hCM, POS_GO2VIEW);
		::ModifyMenu(hCM, POS_GO2VIEW, MF_BYPOSITION, cmdID, pGoToView);
	}
	if (pCloneToView && pCloneToView[0])
	{
		int cmdID = ::GetMenuItemID(hCM, POS_CLONE2VIEW);
		::ModifyMenu(hCM, POS_CLONE2VIEW, MF_BYPOSITION, cmdID, pCloneToView);
	}
	if (pGoToNewInst && pGoToNewInst[0])
	{
		int cmdID = ::GetMenuItemID(hCM, POS_GO2NEWINST);
		::ModifyMenu(hCM, POS_GO2NEWINST, MF_BYPOSITION, cmdID, pGoToNewInst);
	}
	if (pOpenInNewInst && pOpenInNewInst[0])
	{
		int cmdID = ::GetMenuItemID(hCM, POS_OPENINNEWINST);
		::ModifyMenu(hCM, POS_OPENINNEWINST, MF_BYPOSITION, cmdID, pOpenInNewInst);
	}
	if (pClose && pClose[0])
	{
		int cmdID = ::GetMenuItemID(hCM, POS_CLOSE);
		::ModifyMenu(hCM, POS_CLOSE, MF_BYPOSITION, cmdID, pClose);
	}
	if (pCloseBut && pCloseBut[0])
	{
		int cmdID = ::GetMenuItemID(hCM, POS_CLOSEBUT);
		::ModifyMenu(hCM, POS_CLOSEBUT, MF_BYPOSITION, cmdID, pCloseBut);
	}
	if (pSave && pSave[0])
	{
		int cmdID = ::GetMenuItemID(hCM, POS_SAVE);
		::ModifyMenu(hCM, POS_SAVE, MF_BYPOSITION, cmdID, pSave);
	}
	if (pSaveAs && pSaveAs[0])
	{
		int cmdID = ::GetMenuItemID(hCM, POS_SAVEAS);
		::ModifyMenu(hCM, POS_SAVEAS, MF_BYPOSITION, cmdID, pSaveAs);
	}
	if (pPrint && pPrint[0])
	{
		int cmdID = ::GetMenuItemID(hCM, POS_PRINT);
		::ModifyMenu(hCM, POS_PRINT, MF_BYPOSITION, cmdID, pPrint);
	}
	if (pClearReadOnly && pClearReadOnly[0])
	{
		int cmdID = ::GetMenuItemID(hCM, POS_CLEARREADONLY);
		::ModifyMenu(hCM, POS_CLEARREADONLY, MF_BYPOSITION, cmdID, pClearReadOnly);
	}
	if (pReadOnly && pReadOnly[0])
	{
		int cmdID = ::GetMenuItemID(hCM, POS_READONLY);
		::ModifyMenu(hCM, POS_READONLY, MF_BYPOSITION, cmdID, pReadOnly);
	}
	if (pCilpFullPath && pCilpFullPath[0])
	{
		int cmdID = ::GetMenuItemID(hCM, POS_CLIPFULLPATH);
		::ModifyMenu(hCM, POS_CLIPFULLPATH, MF_BYPOSITION, cmdID, pCilpFullPath);
	}
	if (pCilpFileName && pCilpFileName[0])
	{
		int cmdID = ::GetMenuItemID(hCM, POS_CLIPFILENAME);
		::ModifyMenu(hCM, POS_CLIPFILENAME, MF_BYPOSITION, cmdID, pCilpFileName);
	}
	if (pCilpCurrentDir && pCilpCurrentDir[0])
	{
		int cmdID = ::GetMenuItemID(hCM, POS_CLIPCURRENTDIR);
		::ModifyMenu(hCM, POS_CLIPCURRENTDIR, MF_BYPOSITION, cmdID, pCilpCurrentDir);
	}
	if (pRename && pRename[0])
	{
		int cmdID = ::GetMenuItemID(hCM, POS_RENAME);
		::ModifyMenu(hCM, POS_RENAME, MF_BYPOSITION, cmdID, pRename);
	}
	if (pRemove && pRemove[0])
	{
		int cmdID = ::GetMenuItemID(hCM, POS_REMOVE);
		::ModifyMenu(hCM, POS_REMOVE, MF_BYPOSITION, cmdID, pRemove);
	}
#endif
}

void Notepad_plus::changeLangTabDrapContextMenu()
{
	const int POS_GO2VIEW = 0;
	const int POS_CLONE2VIEW = 1;
	const char *goToViewA = NULL;
	const char *cloneToViewA = NULL;

	if (_nativeLangA)
	{
		TiXmlNodeA *tabBarMenu = _nativeLangA->FirstChild("Menu");
		if (tabBarMenu)
			tabBarMenu = tabBarMenu->FirstChild("TabBar");
		if (tabBarMenu)
		{
			for (TiXmlNodeA *childNode = tabBarMenu->FirstChildElement("Item");
				childNode ;
				childNode = childNode->NextSibling("Item") )
			{
				TiXmlElementA *element = childNode->ToElement();
				int ordre;
				element->Attribute("order", &ordre);
				if (ordre == 5)
					goToViewA = element->Attribute("name");
				else if (ordre == 6)
					cloneToViewA = element->Attribute("name");
			}
		}
		HMENU hCM = _tabPopupDropMenu.getMenuHandle();
#ifdef UNICODE
		WcharMbcsConvertor *wmc = WcharMbcsConvertor::getInstance();
		if (goToViewA && goToViewA[0])
		{
			const wchar_t *goToViewG = wmc->char2wchar(goToViewA, _nativeLangEncoding);
			int cmdID = ::GetMenuItemID(hCM, POS_GO2VIEW);
			::ModifyMenu(hCM, POS_GO2VIEW, MF_BYPOSITION|MF_STRING, cmdID, goToViewG);
		}
		if (cloneToViewA && cloneToViewA[0])
		{
			const wchar_t *cloneToViewG = wmc->char2wchar(cloneToViewA, _nativeLangEncoding);
			int cmdID = ::GetMenuItemID(hCM, POS_CLONE2VIEW);
			::ModifyMenu(hCM, POS_CLONE2VIEW, MF_BYPOSITION|MF_STRING, cmdID, cloneToViewG);
		}
#else
		if (goToViewA && goToViewA[0])
		{
			int cmdID = ::GetMenuItemID(hCM, POS_GO2VIEW);
			::ModifyMenu(hCM, POS_GO2VIEW, MF_BYPOSITION, cmdID, goToViewA);
		}
		if (cloneToViewA && cloneToViewA[0])
		{
			int cmdID = ::GetMenuItemID(hCM, POS_CLONE2VIEW);
			::ModifyMenu(hCM, POS_CLONE2VIEW, MF_BYPOSITION, cmdID, cloneToViewA);
		}
#endif
	}
}

void Notepad_plus::changeConfigLang()
{
	if (!_nativeLangA) return;

	TiXmlNodeA *styleConfDlgNode = _nativeLangA->FirstChild("Dialog");
	if (!styleConfDlgNode) return;	
	
	styleConfDlgNode = styleConfDlgNode->FirstChild("StyleConfig");
	if (!styleConfDlgNode) return;

	HWND hDlg = _configStyleDlg.getHSelf();

#ifdef UNICODE
	WcharMbcsConvertor *wmc = WcharMbcsConvertor::getInstance();
#endif

	// Set Title
	const char *titre = (styleConfDlgNode->ToElement())->Attribute("title");

	if ((titre && titre[0]) && hDlg)
	{
#ifdef UNICODE
		const wchar_t *nameW = wmc->char2wchar(titre, _nativeLangEncoding);
		::SetWindowText(hDlg, nameW);
#else
		::SetWindowText(hDlg, titre);
#endif
	}
	for (TiXmlNodeA *childNode = styleConfDlgNode->FirstChildElement("Item");
		childNode ;
		childNode = childNode->NextSibling("Item") )
	{
		TiXmlElementA *element = childNode->ToElement();
		int id;
		const char *sentinel = element->Attribute("id", &id);
		const char *name = element->Attribute("name");
		if (sentinel && (name && name[0]))
		{
			HWND hItem = ::GetDlgItem(hDlg, id);
			if (hItem)
			{
#ifdef UNICODE
				const wchar_t *nameW = wmc->char2wchar(name, _nativeLangEncoding);
				::SetWindowText(hItem, nameW);
#else
				::SetWindowText(hItem, name);
#endif
			}
		}
	}
	hDlg = _configStyleDlg.getHSelf();
	styleConfDlgNode = styleConfDlgNode->FirstChild("SubDialog");
	
	for (TiXmlNodeA *childNode = styleConfDlgNode->FirstChildElement("Item");
		childNode ;
		childNode = childNode->NextSibling("Item") )
	{
		TiXmlElementA *element = childNode->ToElement();
		int id;
		const char *sentinel = element->Attribute("id", &id);
		const char *name = element->Attribute("name");
		if (sentinel && (name && name[0]))
		{
			HWND hItem = ::GetDlgItem(hDlg, id);
			if (hItem)
			{
#ifdef UNICODE
				const wchar_t *nameW = wmc->char2wchar(name, _nativeLangEncoding);
				::SetWindowText(hItem, nameW);
#else
				::SetWindowText(hItem, name);
#endif
			}
		}
	}
}


void Notepad_plus::changeStyleCtrlsLang(HWND hDlg, int *idArray, const char **translatedText)
{
	const int iColorStyle = 0;
	const int iUnderline = 8;

	HWND hItem;
	for (int i = iColorStyle ; i < (iUnderline + 1) ; i++)
	{
		if (translatedText[i] && translatedText[i][0])
		{
			hItem = ::GetDlgItem(hDlg, idArray[i]);
			if (hItem)
			{
#ifdef UNICODE
				WcharMbcsConvertor *wmc = WcharMbcsConvertor::getInstance();
				const wchar_t *nameW = wmc->char2wchar(translatedText[i], _nativeLangEncoding);
				::SetWindowText(hItem, nameW);
#else
				::SetWindowText(hItem, translatedText[i]);
#endif
				
			}
		}
	}
}

void Notepad_plus::changeUserDefineLang()
{
	if (!_nativeLangA) return;

	TiXmlNodeA *userDefineDlgNode = _nativeLangA->FirstChild("Dialog");
	if (!userDefineDlgNode) return;	
	
	userDefineDlgNode = userDefineDlgNode->FirstChild("UserDefine");
	if (!userDefineDlgNode) return;

	UserDefineDialog *userDefineDlg = _pEditView->getUserDefineDlg();

	HWND hDlg = userDefineDlg->getHSelf();
#ifdef UNICODE
	WcharMbcsConvertor *wmc = WcharMbcsConvertor::getInstance();
#endif

	// Set Title
	const char *titre = (userDefineDlgNode->ToElement())->Attribute("title");
	if (titre && titre[0])
	{
#ifdef UNICODE
		const wchar_t *nameW = wmc->char2wchar(titre, _nativeLangEncoding);
		::SetWindowText(hDlg, nameW);
#else
		::SetWindowText(hDlg, titre);
#endif
	}
	// pour ses propres controls 	
	const int nbControl = 9;
	const char *translatedText[nbControl];
	for (int i = 0 ; i < nbControl ; i++)
		translatedText[i] = NULL;

	for (TiXmlNodeA *childNode = userDefineDlgNode->FirstChildElement("Item");
		childNode ;
		childNode = childNode->NextSibling("Item") )
	{
		TiXmlElementA *element = childNode->ToElement();
		int id;
		const char *sentinel = element->Attribute("id", &id);
		const char *name = element->Attribute("name");
		
		if (sentinel && (name && name[0]))
		{
			if (id > 30)
			{
				HWND hItem = ::GetDlgItem(hDlg, id);
				if (hItem)
				{
#ifdef UNICODE
					const wchar_t *nameW = wmc->char2wchar(name, _nativeLangEncoding);
					::SetWindowText(hItem, nameW);
#else
					::SetWindowText(hItem, name);
#endif
				}
			}
			else
			{
				switch(id)
				{
					case 0: case 1: case 2: case 3: case 4:
					case 5: case 6: case 7: case 8: 
 						translatedText[id] = name; break;
				}
			}
		}
	}

	const int nbDlg = 4;
	HWND hDlgArrary[nbDlg];
	hDlgArrary[0] = userDefineDlg->getFolderHandle();
	hDlgArrary[1] = userDefineDlg->getKeywordsHandle();
	hDlgArrary[2] = userDefineDlg->getCommentHandle();
	hDlgArrary[3] = userDefineDlg->getSymbolHandle();
	
	const int nbGrpFolder = 3;
	int folderID[nbGrpFolder][nbControl] = {\
		{IDC_DEFAULT_COLORSTYLEGROUP_STATIC, IDC_DEFAULT_FG_STATIC, IDC_DEFAULT_BG_STATIC, IDC_DEFAULT_FONTSTYLEGROUP_STATIC, IDC_DEFAULT_FONTNAME_STATIC, IDC_DEFAULT_FONTSIZE_STATIC, IDC_DEFAULT_BOLD_CHECK, IDC_DEFAULT_ITALIC_CHECK, IDC_DEFAULT_UNDERLINE_CHECK},\
		{IDC_FOLDEROPEN_COLORSTYLEGROUP_STATIC, IDC_FOLDEROPEN_FG_STATIC, IDC_FOLDEROPEN_BG_STATIC, IDC_FOLDEROPEN_FONTSTYLEGROUP_STATIC, IDC_FOLDEROPEN_FONTNAME_STATIC, IDC_FOLDEROPEN_FONTSIZE_STATIC, IDC_FOLDEROPEN_BOLD_CHECK, IDC_FOLDEROPEN_ITALIC_CHECK, IDC_FOLDEROPEN_UNDERLINE_CHECK},\
		{IDC_FOLDERCLOSE_COLORSTYLEGROUP_STATIC, IDC_FOLDERCLOSE_FG_STATIC, IDC_FOLDERCLOSE_BG_STATIC, IDC_FOLDERCLOSE_FONTSTYLEGROUP_STATIC, IDC_FOLDERCLOSE_FONTNAME_STATIC, IDC_FOLDERCLOSE_FONTSIZE_STATIC, IDC_FOLDERCLOSE_BOLD_CHECK, IDC_FOLDERCLOSE_ITALIC_CHECK, IDC_FOLDERCLOSE_UNDERLINE_CHECK}\
	};

	const int nbGrpKeywords = 4;
	int keywordsID[nbGrpKeywords][nbControl] = {\
		 {IDC_KEYWORD1_COLORSTYLEGROUP_STATIC, IDC_KEYWORD1_FG_STATIC, IDC_KEYWORD1_BG_STATIC, IDC_KEYWORD1_FONTSTYLEGROUP_STATIC, IDC_KEYWORD1_FONTNAME_STATIC, IDC_KEYWORD1_FONTSIZE_STATIC, IDC_KEYWORD1_BOLD_CHECK, IDC_KEYWORD1_ITALIC_CHECK, IDC_KEYWORD1_UNDERLINE_CHECK},\
		{IDC_KEYWORD2_COLORSTYLEGROUP_STATIC, IDC_KEYWORD2_FG_STATIC, IDC_KEYWORD2_BG_STATIC, IDC_KEYWORD2_FONTSTYLEGROUP_STATIC, IDC_KEYWORD2_FONTNAME_STATIC, IDC_KEYWORD2_FONTSIZE_STATIC, IDC_KEYWORD2_BOLD_CHECK, IDC_KEYWORD2_ITALIC_CHECK, IDC_KEYWORD2_UNDERLINE_CHECK},\
		{IDC_KEYWORD3_COLORSTYLEGROUP_STATIC, IDC_KEYWORD3_FG_STATIC, IDC_KEYWORD3_BG_STATIC, IDC_KEYWORD3_FONTSTYLEGROUP_STATIC, IDC_KEYWORD3_FONTNAME_STATIC, IDC_KEYWORD3_FONTSIZE_STATIC, IDC_KEYWORD3_BOLD_CHECK, IDC_KEYWORD3_ITALIC_CHECK, IDC_KEYWORD3_UNDERLINE_CHECK},\
		{IDC_KEYWORD4_COLORSTYLEGROUP_STATIC, IDC_KEYWORD4_FG_STATIC, IDC_KEYWORD4_BG_STATIC, IDC_KEYWORD4_FONTSTYLEGROUP_STATIC, IDC_KEYWORD4_FONTNAME_STATIC, IDC_KEYWORD4_FONTSIZE_STATIC, IDC_KEYWORD4_BOLD_CHECK, IDC_KEYWORD4_ITALIC_CHECK, IDC_KEYWORD4_UNDERLINE_CHECK}\
	};

	const int nbGrpComment = 3;
	int commentID[nbGrpComment][nbControl] = {\
		{IDC_COMMENT_COLORSTYLEGROUP_STATIC, IDC_COMMENT_FG_STATIC, IDC_COMMENT_BG_STATIC, IDC_COMMENT_FONTSTYLEGROUP_STATIC, IDC_COMMENT_FONTNAME_STATIC, IDC_COMMENT_FONTSIZE_STATIC, IDC_COMMENT_BOLD_CHECK, IDC_COMMENT_ITALIC_CHECK, IDC_COMMENT_UNDERLINE_CHECK},\
		{IDC_NUMBER_COLORSTYLEGROUP_STATIC, IDC_NUMBER_FG_STATIC, IDC_NUMBER_BG_STATIC, IDC_NUMBER_FONTSTYLEGROUP_STATIC, IDC_NUMBER_FONTNAME_STATIC, IDC_NUMBER_FONTSIZE_STATIC, IDC_NUMBER_BOLD_CHECK, IDC_NUMBER_ITALIC_CHECK, IDC_NUMBER_UNDERLINE_CHECK},\
		{IDC_COMMENTLINE_COLORSTYLEGROUP_STATIC, IDC_COMMENTLINE_FG_STATIC, IDC_COMMENTLINE_BG_STATIC, IDC_COMMENTLINE_FONTSTYLEGROUP_STATIC, IDC_COMMENTLINE_FONTNAME_STATIC, IDC_COMMENTLINE_FONTSIZE_STATIC, IDC_COMMENTLINE_BOLD_CHECK, IDC_COMMENTLINE_ITALIC_CHECK, IDC_COMMENTLINE_UNDERLINE_CHECK}\
	};

	const int nbGrpOperator = 3;
	int operatorID[nbGrpOperator][nbControl] = {\
		{IDC_SYMBOL_COLORSTYLEGROUP_STATIC, IDC_SYMBOL_FG_STATIC, IDC_SYMBOL_BG_STATIC, IDC_SYMBOL_FONTSTYLEGROUP_STATIC, IDC_SYMBOL_FONTNAME_STATIC, IDC_SYMBOL_FONTSIZE_STATIC, IDC_SYMBOL_BOLD_CHECK, IDC_SYMBOL_ITALIC_CHECK, IDC_SYMBOL_UNDERLINE_CHECK},\
		{IDC_SYMBOL_COLORSTYLEGROUP2_STATIC, IDC_SYMBOL_FG2_STATIC, IDC_SYMBOL_BG2_STATIC, IDC_SYMBOL_FONTSTYLEGROUP2_STATIC, IDC_SYMBOL_FONTNAME2_STATIC, IDC_SYMBOL_FONTSIZE2_STATIC, IDC_SYMBOL_BOLD2_CHECK, IDC_SYMBOL_ITALIC2_CHECK, IDC_SYMBOL_UNDERLINE2_CHECK},\
		{IDC_SYMBOL_COLORSTYLEGROUP3_STATIC, IDC_SYMBOL_FG3_STATIC, IDC_SYMBOL_BG3_STATIC, IDC_SYMBOL_FONTSTYLEGROUP3_STATIC, IDC_SYMBOL_FONTNAME3_STATIC, IDC_SYMBOL_FONTSIZE3_STATIC, IDC_SYMBOL_BOLD3_CHECK, IDC_SYMBOL_ITALIC3_CHECK, IDC_SYMBOL_UNDERLINE3_CHECK}
	};
	
	int nbGpArray[nbDlg] = {nbGrpFolder, nbGrpKeywords, nbGrpComment, nbGrpOperator};
	const char nodeNameArray[nbDlg][16] = {"Folder", "Keywords", "Comment", "Operator"};

	for (int i = 0 ; i < nbDlg ; i++)
	{
	
		for (int j = 0 ; j < nbGpArray[i] ; j++)
		{
			switch (i)
			{
				case 0 : changeStyleCtrlsLang(hDlgArrary[i], folderID[j], translatedText); break;
				case 1 : changeStyleCtrlsLang(hDlgArrary[i], keywordsID[j], translatedText); break;
				case 2 : changeStyleCtrlsLang(hDlgArrary[i], commentID[j], translatedText); break;
				case 3 : changeStyleCtrlsLang(hDlgArrary[i], operatorID[j], translatedText); break;
			}
		}
		TiXmlNodeA *node = userDefineDlgNode->FirstChild(nodeNameArray[i]);
		
		if (node) 
		{
			// Set Title
			titre = (node->ToElement())->Attribute("title");
			if (titre &&titre[0])
			{
#ifdef UNICODE
				const wchar_t *nameW = wmc->char2wchar(titre, _nativeLangEncoding);
				userDefineDlg->setTabName(i, nameW);
#else
				userDefineDlg->setTabName(i, titre);
#endif
			}
			for (TiXmlNodeA *childNode = node->FirstChildElement("Item");
				childNode ;
				childNode = childNode->NextSibling("Item") )
			{
				TiXmlElementA *element = childNode->ToElement();
				int id;
				const char *sentinel = element->Attribute("id", &id);
				const char *name = element->Attribute("name");
				if (sentinel && (name && name[0]))
				{
					HWND hItem = ::GetDlgItem(hDlgArrary[i], id);
					if (hItem)
					{
#ifdef UNICODE
						const wchar_t *nameW = wmc->char2wchar(name, _nativeLangEncoding);
						::SetWindowText(hItem, nameW);
#else
						::SetWindowText(hItem, name);
#endif
					}
				}
			}
		}
	}
}

void Notepad_plus::changeFindReplaceDlgLang()
{
	if (_nativeLangA)
	{
		TiXmlNodeA *dlgNode = _nativeLangA->FirstChild("Dialog");
		if (dlgNode)
		{
			NppParameters *pNppParam = NppParameters::getInstance();
			dlgNode = searchDlgNode(dlgNode, "Find");
			if (dlgNode)
			{
				const char *titre1 = (dlgNode->ToElement())->Attribute("titleFind");
				const char *titre2 = (dlgNode->ToElement())->Attribute("titleReplace");
				const char *titre3 = (dlgNode->ToElement())->Attribute("titleFindInFiles");
				if (titre1 && titre2 && titre3)
				{
#ifdef UNICODE
					WcharMbcsConvertor *wmc = WcharMbcsConvertor::getInstance();

					basic_string<wchar_t> nameW = wmc->char2wchar(titre1, _nativeLangEncoding);
					pNppParam->getFindDlgTabTitiles()._find = nameW;

					nameW = wmc->char2wchar(titre2, _nativeLangEncoding);
					pNppParam->getFindDlgTabTitiles()._replace = nameW;

					nameW = wmc->char2wchar(titre3, _nativeLangEncoding);
					pNppParam->getFindDlgTabTitiles()._findInFiles = nameW;
#else
					pNppParam->getFindDlgTabTitiles()._find = titre1;
					pNppParam->getFindDlgTabTitiles()._replace = titre2;
					pNppParam->getFindDlgTabTitiles()._findInFiles = titre3;
#endif
				}
			}

			_findReplaceDlg.changeTabName(FIND_DLG, pNppParam->getFindDlgTabTitiles()._find.c_str());
			_findReplaceDlg.changeTabName(REPLACE_DLG, pNppParam->getFindDlgTabTitiles()._replace.c_str());
			_findReplaceDlg.changeTabName(FINDINFILES_DLG, pNppParam->getFindDlgTabTitiles()._findInFiles.c_str());
		}
	}
	changeDlgLang(_findReplaceDlg.getHSelf(), "Find");
}

void Notepad_plus::changePrefereceDlgLang() 
{
	changeDlgLang(_preference.getHSelf(), "Preference");

	char titre[128];

#ifdef UNICODE
	WcharMbcsConvertor *wmc = WcharMbcsConvertor::getInstance();
#endif

	changeDlgLang(_preference._barsDlg.getHSelf(), "Global", titre);
	if (*titre)
	{
#ifdef UNICODE
		const wchar_t *nameW = wmc->char2wchar(titre, _nativeLangEncoding);
		_preference._ctrlTab.renameTab(TEXT("Global"), nameW);
#else
		_preference._ctrlTab.renameTab("Global", titre);
#endif
	}
	changeDlgLang(_preference._marginsDlg.getHSelf(), "Scintillas", titre);
	if (*titre)
	{
#ifdef UNICODE
		const wchar_t *nameW = wmc->char2wchar(titre, _nativeLangEncoding);
		_preference._ctrlTab.renameTab(TEXT("Scintillas"), nameW);
#else
		_preference._ctrlTab.renameTab("Scintillas", titre);
#endif
	}

	changeDlgLang(_preference._defaultNewDocDlg.getHSelf(), "NewDoc", titre);
	if (*titre)
	{
#ifdef UNICODE
		const wchar_t *nameW = wmc->char2wchar(titre, _nativeLangEncoding);
		_preference._ctrlTab.renameTab(TEXT("NewDoc"), nameW);
#else
		_preference._ctrlTab.renameTab("NewDoc", titre);
#endif
	}

	changeDlgLang(_preference._fileAssocDlg.getHSelf(), "FileAssoc", titre);
	if (*titre)
	{
#ifdef UNICODE
		const wchar_t *nameW = wmc->char2wchar(titre, _nativeLangEncoding);
		_preference._ctrlTab.renameTab(TEXT("FileAssoc"), nameW);
#else
		_preference._ctrlTab.renameTab("FileAssoc", titre);
#endif
	}

	changeDlgLang(_preference._langMenuDlg.getHSelf(), "LangMenu", titre);
	if (*titre)
	{
#ifdef UNICODE
		const wchar_t *nameW = wmc->char2wchar(titre, _nativeLangEncoding);
		_preference._ctrlTab.renameTab(TEXT("LangMenu"), nameW);
#else
		_preference._ctrlTab.renameTab("LangMenu", titre);
#endif
	}

	changeDlgLang(_preference._printSettingsDlg.getHSelf(), "Print", titre);
	if (*titre)
	{
#ifdef UNICODE
		const wchar_t *nameW = wmc->char2wchar(titre, _nativeLangEncoding);
		_preference._ctrlTab.renameTab(TEXT("Print"), nameW);
#else
		_preference._ctrlTab.renameTab("Print", titre);
#endif
	}
/*
	changeDlgLang(_preference._printSettings2Dlg.getHSelf(), "Print2", titre);
	if (*titre)
	{
#ifdef UNICODE
		const wchar_t *nameW = wmc->char2wchar(titre, _nativeLangEncoding);
		_preference._ctrlTab.renameTab(TEXT("Print2"), nameW);
#else
		_preference._ctrlTab.renameTab("Print2", titre);
#endif
	}
*/
	changeDlgLang(_preference._settingsDlg.getHSelf(), "MISC", titre);
	if (*titre)
	{
#ifdef UNICODE
		const wchar_t *nameW = wmc->char2wchar(titre, _nativeLangEncoding);
		_preference._ctrlTab.renameTab(TEXT("MISC"), nameW);
#else
		_preference._ctrlTab.renameTab("MISC", titre);
#endif
	}
	changeDlgLang(_preference._backupDlg.getHSelf(), "Backup", titre);
	if (*titre)
	{
#ifdef UNICODE
		const wchar_t *nameW = wmc->char2wchar(titre, _nativeLangEncoding);
		_preference._ctrlTab.renameTab(TEXT("Backup"), nameW);
#else
		_preference._ctrlTab.renameTab("Backup", titre);
#endif
	}
}

void Notepad_plus::changeShortcutLang()
{
	if (!_nativeLangA) return;

	NppParameters * pNppParam = NppParameters::getInstance();
	vector<CommandShortcut> & mainshortcuts = pNppParam->getUserShortcuts();
	vector<ScintillaKeyMap> & scinshortcuts = pNppParam->getScintillaKeyList();
	int mainSize = (int)mainshortcuts.size();
	int scinSize = (int)scinshortcuts.size();

	TiXmlNodeA *shortcuts = _nativeLangA->FirstChild("Shortcuts");
	if (!shortcuts) return;

	shortcuts = shortcuts->FirstChild("Main");
	if (!shortcuts) return;

	TiXmlNodeA *entriesRoot = shortcuts->FirstChild("Entries");
	if (!entriesRoot) return;

	for (TiXmlNodeA *childNode = entriesRoot->FirstChildElement("Item");
		childNode ;
		childNode = childNode->NextSibling("Item") )
	{
		TiXmlElementA *element = childNode->ToElement();
		int index, id;
		if (element->Attribute("index", &index) && element->Attribute("id", &id))
		{
			if (index > -1 && index < mainSize) { //valid index only
				const char *name = element->Attribute("name");
				CommandShortcut & csc = mainshortcuts[index];
				if (csc.getID() == (unsigned long)id) 
				{
#ifdef UNICODE
					WcharMbcsConvertor *wmc = WcharMbcsConvertor::getInstance();
					const wchar_t * nameW = wmc->char2wchar(name, _nativeLangEncoding);
					csc.setName(nameW);
#else
					csc.setName(name);
#endif
				}
			}
		}
	}

	//Scintilla
	shortcuts = _nativeLangA->FirstChild("Shortcuts");
	if (!shortcuts) return;

	shortcuts = shortcuts->FirstChild("Scintilla");
	if (!shortcuts) return;

	entriesRoot = shortcuts->FirstChild("Entries");
	if (!entriesRoot) return;

	for (TiXmlNodeA *childNode = entriesRoot->FirstChildElement("Item");
		childNode ;
		childNode = childNode->NextSibling("Item") )
	{
		TiXmlElementA *element = childNode->ToElement();
		int index;
		if (element->Attribute("index", &index))
		{
			if (index > -1 && index < scinSize) { //valid index only
				const char *name = element->Attribute("name");
				ScintillaKeyMap & skm = scinshortcuts[index];
#ifdef UNICODE
				WcharMbcsConvertor *wmc = WcharMbcsConvertor::getInstance();
				const wchar_t * nameW = wmc->char2wchar(name, _nativeLangEncoding);
				skm.setName(nameW);
#else
				skm.setName(name);
#endif
			}
		}
	}

}

void Notepad_plus::changeShortcutmapperLang(ShortcutMapper * sm)
{
	if (!_nativeLangA) return;

	TiXmlNodeA *shortcuts = _nativeLangA->FirstChild("Dialog");
	if (!shortcuts) return;

	shortcuts = shortcuts->FirstChild("ShortcutMapper");
	if (!shortcuts) return;

	for (TiXmlNodeA *childNode = shortcuts->FirstChildElement("Item");
		childNode ;
		childNode = childNode->NextSibling("Item") )
	{
		TiXmlElementA *element = childNode->ToElement();
		int index;
		if (element->Attribute("index", &index))
		{
			if (index > -1 && index < 5)  //valid index only
			{
				const char *name = element->Attribute("name");

#ifdef UNICODE
				WcharMbcsConvertor *wmc = WcharMbcsConvertor::getInstance();
				const wchar_t * nameW = wmc->char2wchar(name, _nativeLangEncoding);
				sm->translateTab(index, nameW);
#else
				sm->translateTab(index, name);
#endif
			}
		}
	}
}


TiXmlNodeA * Notepad_plus::searchDlgNode(TiXmlNodeA *node, const char *dlgTagName)
{
	TiXmlNodeA *dlgNode = node->FirstChild(dlgTagName);
	if (dlgNode) return dlgNode;
	for (TiXmlNodeA *childNode = node->FirstChildElement();
		childNode ;
		childNode = childNode->NextSibling() )
	{
		dlgNode = searchDlgNode(childNode, dlgTagName);
		if (dlgNode) return dlgNode;
	}
	return NULL;
}

bool Notepad_plus::changeDlgLang(HWND hDlg, const char *dlgTagName, char *title)
{
	if (title)
		title[0] = '\0';

	if (!_nativeLangA) return false;

	TiXmlNodeA *dlgNode = _nativeLangA->FirstChild("Dialog");
	if (!dlgNode) return false;

	dlgNode = searchDlgNode(dlgNode, dlgTagName);
	if (!dlgNode) return false;

#ifdef UNICODE
	WcharMbcsConvertor *wmc = WcharMbcsConvertor::getInstance();
#endif

	// Set Title
	const char *titre = (dlgNode->ToElement())->Attribute("title");
	if ((titre && titre[0]) && hDlg)
	{
#ifdef UNICODE
		const wchar_t *nameW = wmc->char2wchar(titre, _nativeLangEncoding);
		::SetWindowText(hDlg, nameW);
#else
		::SetWindowText(hDlg, titre);
#endif
		if (title)
			strcpy(title, titre);
	}

	// Set the text of child control
	for (TiXmlNodeA *childNode = dlgNode->FirstChildElement("Item");
		childNode ;
		childNode = childNode->NextSibling("Item") )
	{
		TiXmlElementA *element = childNode->ToElement();
		int id;
		const char *sentinel = element->Attribute("id", &id);
		const char *name = element->Attribute("name");
		if (sentinel && (name && name[0]))
		{
			HWND hItem = ::GetDlgItem(hDlg, id);
			if (hItem)
			{
#ifdef UNICODE
				const wchar_t *nameW = wmc->char2wchar(name, _nativeLangEncoding);
				::SetWindowText(hItem, nameW);
#else
				::SetWindowText(hItem, name);
#endif
			}
		}
	}
	return true;
}

static generic_string extractSymbol(TCHAR prefix, const TCHAR *str2extract)
{
	bool found = false;
	TCHAR extracted[128] = TEXT("");

	for (int i = 0, j = 0 ; i < lstrlen(str2extract) ; i++)
	{
		if (found)
		{
			if (!str2extract[i] || str2extract[i] == ' ')
			{
				extracted[j] = '\0';
				return generic_string(extracted);
			}
			extracted[j++] = str2extract[i];

		}
		else
		{
			if (!str2extract[i])
				return TEXT("");

			if (str2extract[i] == prefix)
				found = true;
		}
	}
	return  generic_string(extracted);
};

bool Notepad_plus::doBlockComment(comment_mode currCommentMode)
{
	const TCHAR *commentLineSybol;
	generic_string symbol;

	Buffer * buf = _pEditView->getCurrentBuffer();
	if (buf->getLangType() == L_USER)
	{
		UserLangContainer * userLangContainer = NppParameters::getInstance()->getULCFromName(buf->getUserDefineLangName());
		if (!userLangContainer)
			return false;

		symbol = extractSymbol('0', userLangContainer->_keywordLists[4]);
		commentLineSybol = symbol.c_str();
	}
	else
		commentLineSybol = buf->getCommentLineSymbol();


	if ((!commentLineSybol) || (!commentLineSybol[0]))
		return false;

    generic_string comment(commentLineSybol);
    comment += TEXT(" ");
    
	const int linebufferSize = 1000;
    TCHAR linebuf[linebufferSize];
    size_t comment_length = comment.length();
    size_t selectionStart = _pEditView->execute(SCI_GETSELECTIONSTART);
    size_t selectionEnd = _pEditView->execute(SCI_GETSELECTIONEND);
    size_t caretPosition = _pEditView->execute(SCI_GETCURRENTPOS);
    // checking if caret is located in _beginning_ of selected block
    bool move_caret = caretPosition < selectionEnd;
    int selStartLine = _pEditView->execute(SCI_LINEFROMPOSITION, selectionStart);
    int selEndLine = _pEditView->execute(SCI_LINEFROMPOSITION, selectionEnd);
    int lines = selEndLine - selStartLine;
    size_t firstSelLineStart = _pEditView->execute(SCI_POSITIONFROMLINE, selStartLine);
    // "caret return" is part of the last selected line
    if ((lines > 0) && (selectionEnd == static_cast<size_t>(_pEditView->execute(SCI_POSITIONFROMLINE, selEndLine))))
		selEndLine--;
    _pEditView->execute(SCI_BEGINUNDOACTION);

    for (int i = selStartLine; i <= selEndLine; i++) 
	{
		int lineStart = _pEditView->execute(SCI_POSITIONFROMLINE, i);
        int lineIndent = lineStart;
        int lineEnd = _pEditView->execute(SCI_GETLINEENDPOSITION, i);
        if ((lineEnd - lineIndent) >= linebufferSize)        // Avoid buffer size problems
                continue;

        lineIndent = _pEditView->execute(SCI_GETLINEINDENTPOSITION, i);
		_pEditView->getGenericText(linebuf, lineIndent, lineEnd);
        
        generic_string linebufStr = linebuf;

        // empty lines are not commented
        if (lstrlen(linebuf) < 1)
			continue;
   		if (currCommentMode != cm_comment)
		{
            if (linebufStr.substr(0, comment_length - 1) == comment.substr(0, comment_length - 1))
				{
                int len = (linebufStr.substr(0, comment_length) == comment)?comment_length:comment_length - 1;
					
                _pEditView->execute(SCI_SETSEL, lineIndent, lineIndent + len);
					_pEditView->replaceSelWith("");
				
					if (i == selStartLine) // is this the first selected line?
					selectionStart -= len;
				selectionEnd -= len; // every iteration
					continue;
				}
			}
		if ((currCommentMode == cm_toggle) || (currCommentMode == cm_comment))
		{
			if (i == selStartLine) // is this the first selected line?
				selectionStart += comment_length;
			selectionEnd += comment_length; // every iteration
			_pEditView->insertGenericTextFrom(lineIndent, comment.c_str());
		}
     }
    // after uncommenting selection may promote itself to the lines
    // before the first initially selected line;
    // another problem - if only comment symbol was selected;
    if (selectionStart < firstSelLineStart)
	{
        if (selectionStart >= selectionEnd - (comment_length - 1))
			selectionEnd = firstSelLineStart;
        selectionStart = firstSelLineStart;
    }
    if (move_caret) 
	{
        // moving caret to the beginning of selected block
        _pEditView->execute(SCI_GOTOPOS, selectionEnd);
        _pEditView->execute(SCI_SETCURRENTPOS, selectionStart);
    }
	else 
	{
        _pEditView->execute(SCI_SETSEL, selectionStart, selectionEnd);
    }
    _pEditView->execute(SCI_ENDUNDOACTION);
    return true;
}

bool Notepad_plus::doStreamComment()
{
	const TCHAR *commentStart;
	const TCHAR *commentEnd;

	generic_string symbolStart;
	generic_string symbolEnd;

	Buffer * buf = _pEditView->getCurrentBuffer();
	if (buf->getLangType() == L_USER)
	{
		UserLangContainer * userLangContainer = NppParameters::getInstance()->getULCFromName(buf->getUserDefineLangName());

		if (!userLangContainer)
			return false;

		symbolStart = extractSymbol('1', userLangContainer->_keywordLists[4]);
		commentStart = symbolStart.c_str();
		symbolEnd = extractSymbol('2', userLangContainer->_keywordLists[4]);
		commentEnd = symbolEnd.c_str();
	}
	else
	{
		commentStart = buf->getCommentStart();
		commentEnd = buf->getCommentEnd();
	}

	if ((!commentStart) || (!commentStart[0]))
		return false;
	if ((!commentEnd) || (!commentEnd[0]))
		return false;

	generic_string start_comment(commentStart);
	generic_string end_comment(commentEnd);
	generic_string white_space(TEXT(" "));

	start_comment += white_space;
	white_space += end_comment;
	end_comment = white_space;
	size_t start_comment_length = start_comment.length();
	size_t selectionStart = _pEditView->execute(SCI_GETSELECTIONSTART);
	size_t selectionEnd = _pEditView->execute(SCI_GETSELECTIONEND);
	size_t caretPosition = _pEditView->execute(SCI_GETCURRENTPOS);
	// checking if caret is located in _beginning_ of selected block
	bool move_caret = caretPosition < selectionEnd;
	// if there is no selection?
	if (selectionEnd - selectionStart <= 0)
	{
		int selLine = _pEditView->execute(SCI_LINEFROMPOSITION, selectionStart);
		int lineIndent = _pEditView->execute(SCI_GETLINEINDENTPOSITION, selLine);
		int lineEnd = _pEditView->execute(SCI_GETLINEENDPOSITION, selLine);

		TCHAR linebuf[1000];
		_pEditView->getGenericText(linebuf, lineIndent, lineEnd);
	    
		int caret = _pEditView->execute(SCI_GETCURRENTPOS);
		int line = _pEditView->execute(SCI_LINEFROMPOSITION, caret);
		int lineStart = _pEditView->execute(SCI_POSITIONFROMLINE, line);
		int current = caret - lineStart;
		// checking if we are not inside a word

		int startword = current;
		int endword = current;
		int start_counter = 0;
		int end_counter = 0;
		while (startword > 0)// && wordCharacters.contains(linebuf[startword - 1]))
		{
			start_counter++;
			startword--;
		}
		// checking _beginning_ of the word
		if (startword == current)
				return true; // caret is located _before_ a word
		while (linebuf[endword + 1] != '\0') // && wordCharacters.contains(linebuf[endword + 1]))
		{
			end_counter++;
			endword++;
		}
		selectionStart -= start_counter;
		selectionEnd += (end_counter + 1);
	}
	_pEditView->execute(SCI_BEGINUNDOACTION);
	_pEditView->insertGenericTextFrom(selectionStart, start_comment.c_str());
	selectionEnd += start_comment_length;
	selectionStart += start_comment_length;
	_pEditView->insertGenericTextFrom(selectionEnd, end_comment.c_str());
	if (move_caret)
	{
		// moving caret to the beginning of selected block
		_pEditView->execute(SCI_GOTOPOS, selectionEnd);
		_pEditView->execute(SCI_SETCURRENTPOS, selectionStart);
	}
	else
	{
		_pEditView->execute(SCI_SETSEL, selectionStart, selectionEnd);
	}
	_pEditView->execute(SCI_ENDUNDOACTION);
	return true;
}

bool Notepad_plus::saveScintillaParams(bool whichOne) 
{
	ScintillaViewParams svp;
	ScintillaEditView *pView = (whichOne == SCIV_PRIMARY)?&_mainEditView:&_subEditView;

	svp._lineNumberMarginShow = pView->hasMarginShowed(ScintillaEditView::_SC_MARGE_LINENUMBER); 
	svp._bookMarkMarginShow = pView->hasMarginShowed(ScintillaEditView::_SC_MARGE_SYBOLE);
	//svp._docChangeStateMarginShow = pView->hasMarginShowed(ScintillaEditView::_SC_MARGE_MODIFMARKER);
	svp._indentGuideLineShow = pView->isShownIndentGuide();
	svp._folderStyle = pView->getFolderStyle();
	svp._currentLineHilitingShow = pView->isCurrentLineHiLiting();
	svp._wrapSymbolShow = pView->isWrapSymbolVisible();
	svp._doWrap = pView->isWrap();
	svp._edgeMode = int(pView->execute(SCI_GETEDGEMODE));
	svp._edgeNbColumn = int(pView->execute(SCI_GETEDGECOLUMN));
	svp._zoom = int(pView->execute(SCI_GETZOOM));
	svp._whiteSpaceShow = pView->isInvisibleCharsShown();
	svp._eolShow = pView->isEolVisible();

	return (NppParameters::getInstance())->writeScintillaParams(svp, whichOne);
}

bool Notepad_plus::addCurrentMacro()
{
	vector<MacroShortcut> & theMacros = (NppParameters::getInstance())->getMacroList();
	
	int nbMacro = theMacros.size();

	int cmdID = ID_MACRO + nbMacro;
	MacroShortcut ms(Shortcut(), _macro, cmdID);
	ms.init(_hInst, _hSelf);

	if (ms.doDialog() != -1)
	{
		HMENU hMacroMenu = ::GetSubMenu(_mainMenuHandle, MENUINDEX_MACRO);
		int const posBase = 6;	//separator at index 5
		if (nbMacro == 0) 
		{
			::InsertMenu(hMacroMenu, posBase-1, MF_BYPOSITION, (unsigned int)-1, 0);	//no separator yet, add one
		}

		theMacros.push_back(ms);
		::InsertMenu(hMacroMenu, posBase + nbMacro, MF_BYPOSITION, cmdID, ms.toMenuItemString().c_str());
		_accelerator.updateShortcuts();
		return true;
	}
	return false;
}

void Notepad_plus::changeToolBarIcons()
{
	if (!_toolIcons)
		return;
	for (int i = 0 ; i < int(_customIconVect.size()) ; i++)
		_toolBar.changeIcons(_customIconVect[i].listIndex, _customIconVect[i].iconIndex, (_customIconVect[i].iconLocation).c_str());
}

bool Notepad_plus::switchToFile(BufferID id)
{
	int i = 0;
	int iView = currentView();
	if (id == BUFFER_INVALID)
		return false;

	if ((i = _pDocTab->getIndexByBuffer(id)) != -1)
	{
		iView = currentView();
	}
	else if ((i = _pNonDocTab->getIndexByBuffer(id)) != -1)
	{
		iView = otherView();
	}

	if (i != -1)
	{
		switchEditViewTo(iView);
		//_pDocTab->activateAt(i);
		activateBuffer(id, currentView());
		return true;
	}
	return false;
}

void Notepad_plus::getTaskListInfo(TaskListInfo *tli)
{
	size_t currentNbDoc = _pDocTab->nbItem();
	size_t nonCurrentNbDoc = _pNonDocTab->nbItem();
	
	tli->_currentIndex = 0;

	if (!viewVisible(otherView()))
		nonCurrentNbDoc = 0;

	for (size_t i = 0 ; i < currentNbDoc ; i++)
	{
		BufferID bufID = _pDocTab->getBufferByIndex(i);
		Buffer * b = MainFileManager->getBufferByID(bufID);
		int status = b->isReadOnly()?tb_ro:(b->isDirty()?tb_unsaved:tb_saved);
		tli->_tlfsLst.push_back(TaskLstFnStatus(currentView(), i, b->getFullPathName(), status));
	}
	for (size_t i = 0 ; i < nonCurrentNbDoc ; i++)
	{
		BufferID bufID = _pNonDocTab->getBufferByIndex(i);
		Buffer * b = MainFileManager->getBufferByID(bufID);
		int status = b->isReadOnly()?tb_ro:(b->isDirty()?tb_unsaved:tb_saved);
		tli->_tlfsLst.push_back(TaskLstFnStatus(otherView(), i, b->getFullPathName(), status));
	}
}

bool Notepad_plus::isDlgsMsg(MSG *msg, bool unicodeSupported) const 
{
	for (size_t i = 0; i < _hModelessDlgs.size(); i++)
	{
		if (unicodeSupported?(::IsDialogMessageW(_hModelessDlgs[i], msg)):(::IsDialogMessageA(_hModelessDlgs[i], msg)))
			return true;
	}
	return false;
}

bool Notepad_plus::goToPreviousIndicator(int indicID2Search, bool isWrap) const
{
    int position = _pEditView->execute(SCI_GETCURRENTPOS);
	int docLen = _pEditView->getCurrentDocLen();

    BOOL isInIndicator = _pEditView->execute(SCI_INDICATORVALUEAT, indicID2Search,  position);
    int posStart = _pEditView->execute(SCI_INDICATORSTART, indicID2Search,  position);
    int posEnd = _pEditView->execute(SCI_INDICATOREND, indicID2Search,  position);

	// pre-condition
	if ((posStart == 0) && (posEnd == docLen - 1))
		return false;

    if (posStart <= 0)
	{
		if (!isWrap)
			return false;

		isInIndicator = _pEditView->execute(SCI_INDICATORVALUEAT, indicID2Search,  docLen - 1);
		posStart = _pEditView->execute(SCI_INDICATORSTART, indicID2Search,  docLen - 1);
	}

    if (isInIndicator) // try to get out of indicator
    {
        posStart = _pEditView->execute(SCI_INDICATORSTART, indicID2Search, posStart - 1);
        if (posStart <= 0)
		{
			if (!isWrap)
				return false;
			posStart = _pEditView->execute(SCI_INDICATORSTART, indicID2Search,  docLen - 1);
		}	
	}

    int newPos = posStart - 1;
    posStart = _pEditView->execute(SCI_INDICATORSTART, indicID2Search, newPos);
    posEnd = _pEditView->execute(SCI_INDICATOREND, indicID2Search, newPos);

	// found
	if (_pEditView->execute(SCI_INDICATORVALUEAT, indicID2Search, posStart))
	{
		NppGUI & nppGUI = (NppGUI &)((NppParameters::getInstance())->getNppGUI());
		nppGUI._disableSmartHiliteTmp = true;

        int currentline = _pEditView->execute(SCI_LINEFROMPOSITION, posEnd);
	    _pEditView->execute(SCI_ENSUREVISIBLE, currentline);	// make sure target line is unfolded

		_pEditView->execute(SCI_SETSEL, posEnd, posStart);
		_pEditView->execute(SCI_SCROLLCARET);
		return true;
	}
	return false;
}

bool Notepad_plus::goToNextIndicator(int indicID2Search, bool isWrap) const
{
    int position = _pEditView->execute(SCI_GETCURRENTPOS);
	int docLen = _pEditView->getCurrentDocLen();

    BOOL isInIndicator = _pEditView->execute(SCI_INDICATORVALUEAT, indicID2Search,  position);
    int posStart = _pEditView->execute(SCI_INDICATORSTART, indicID2Search,  position);
    int posEnd = _pEditView->execute(SCI_INDICATOREND, indicID2Search,  position);

	// pre-condition
	if ((posStart == 0) && (posEnd == docLen - 1))
		return false;

    if (posEnd >= docLen)
	{
		if (!isWrap)
			return false;

		isInIndicator = _pEditView->execute(SCI_INDICATORVALUEAT, indicID2Search,  0);
		posEnd = _pEditView->execute(SCI_INDICATOREND, indicID2Search, 0);
	}

    if (isInIndicator) // try to get out of indicator
    {
        posEnd = _pEditView->execute(SCI_INDICATOREND, indicID2Search, posEnd);

        if (posEnd >= docLen)
		{
			if (!isWrap)
				return false;
			posEnd = _pEditView->execute(SCI_INDICATOREND, indicID2Search, 0);
		}
    }
    int newPos = posEnd + 1;
    posStart = _pEditView->execute(SCI_INDICATORSTART, indicID2Search, newPos);
    posEnd = _pEditView->execute(SCI_INDICATOREND, indicID2Search, newPos);

	// found
	if (_pEditView->execute(SCI_INDICATORVALUEAT, indicID2Search, posStart))
	{
		NppGUI & nppGUI = (NppGUI &)((NppParameters::getInstance())->getNppGUI());
		nppGUI._disableSmartHiliteTmp = true;
		
        int currentline = _pEditView->execute(SCI_LINEFROMPOSITION, posEnd);
	    _pEditView->execute(SCI_ENSUREVISIBLE, currentline);	// make sure target line is unfolded

		_pEditView->execute(SCI_SETSEL, posStart, posEnd);
		_pEditView->execute(SCI_SCROLLCARET);
		return true;
	}
	return false;
}

void Notepad_plus::fullScreenToggle()
{
	if (!_beforeSpecialView.isFullScreen)	//toggle fullscreen on
	{
		_beforeSpecialView._winPlace.length = sizeof(_beforeSpecialView._winPlace);
		::GetWindowPlacement(_hSelf, &_beforeSpecialView._winPlace);

		RECT fullscreenArea;		//RECT used to calculate window fullscreen size
		//Preset view area, in case something fails, primary monitor values
		fullscreenArea.top = 0;
		fullscreenArea.left = 0;
		fullscreenArea.right = GetSystemMetrics(SM_CXSCREEN);	
		fullscreenArea.bottom = GetSystemMetrics(SM_CYSCREEN);
		
		//if (_winVersion != WV_NT)
		{
			HMONITOR currentMonitor;	//Handle to monitor where fullscreen should go
			MONITORINFO mi;				//Info of that monitor
			//Caution, this will not work on windows 95, so probably add some checking of some sorts like Unicode checks, IF 95 were to be supported
			currentMonitor = ::MonitorFromWindow(_hSelf, MONITOR_DEFAULTTONEAREST);	//should always be valid monitor handle
			mi.cbSize = sizeof(MONITORINFO);
			if (::GetMonitorInfo(currentMonitor, &mi) != FALSE)
			{
				fullscreenArea = mi.rcMonitor;
				fullscreenArea.right -= fullscreenArea.left;
				fullscreenArea.bottom -= fullscreenArea.top;
			}
		}

		//Setup GUI
        int bs = buttonStatus_fullscreen;
		if (_beforeSpecialView.isPostIt)
        {
            bs |= buttonStatus_postit;
        }
        else
		{
			//only change the GUI if not already done by postit
			_beforeSpecialView.isMenuShown = ::SendMessage(_hSelf, NPPM_ISMENUHIDDEN, 0, 0) != TRUE;
			if (_beforeSpecialView.isMenuShown)
				::SendMessage(_hSelf, NPPM_HIDEMENU, 0, TRUE);

			//Hide rebar
			_rebarTop.display(false);
			_rebarBottom.display(false);
		}
        _restoreButton.setButtonStatus(bs);

		//Hide window so windows can properly update it
		::ShowWindow(_hSelf, SW_HIDE);

		//Set popup style for fullscreen window and store the old style
		if (!_beforeSpecialView.isPostIt)
		{
			_beforeSpecialView.preStyle = ::SetWindowLongPtr(_hSelf, GWL_STYLE, WS_POPUP);
			if (!_beforeSpecialView.preStyle) {	//something went wrong, use default settings
				_beforeSpecialView.preStyle = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
			}
		}
		
		//Set fullscreen window, highest non-top z-order, show the window and redraw it (refreshing the windowmanager cache aswell)
		::ShowWindow(_hSelf, SW_SHOW);
		::SetWindowPos(_hSelf, HWND_TOP, fullscreenArea.left, fullscreenArea.top, fullscreenArea.right, fullscreenArea.bottom, SWP_NOZORDER|SWP_DRAWFRAME|SWP_FRAMECHANGED);
		::SetForegroundWindow(_hSelf);

        // show restore button
        _restoreButton.doDialog(_isRTL);

        RECT rect;
        GetWindowRect(_restoreButton.getHSelf(), &rect);
	    int w = rect.right - rect.left;
	    int h = rect.bottom - rect.top;

        RECT nppRect;
        GetWindowRect(_hSelf, &nppRect);
        int x = nppRect.right - w;
        int y = nppRect.top;
        ::MoveWindow(_restoreButton.getHSelf(), x, y, w, h, FALSE);
        
        _pEditView->getFocus();
	}
	else	//toggle fullscreen off
	{
		//Hide window for updating, restore style and menu then restore position and Z-Order
		::ShowWindow(_hSelf, SW_HIDE);

        _restoreButton.setButtonStatus(buttonStatus_fullscreen ^ _restoreButton.getButtonStatus());
        _restoreButton.display(false);

		//Setup GUI
		if (!_beforeSpecialView.isPostIt)
		{
			//only change the GUI if postit isnt active
			if (_beforeSpecialView.isMenuShown)
				::SendMessage(_hSelf, NPPM_HIDEMENU, 0, FALSE);

			//Show rebar
			_rebarTop.display(true);
			_rebarBottom.display(true);
		}

		//Set old style if not fullscreen
		if (!_beforeSpecialView.isPostIt)
		{
			::SetWindowLongPtr( _hSelf, GWL_STYLE, _beforeSpecialView.preStyle);
			//Redraw the window and refresh windowmanager cache, dont do anything else, sizing is done later on
			::SetWindowPos(_hSelf, HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_DRAWFRAME|SWP_FRAMECHANGED);
			::ShowWindow(_hSelf, SW_SHOW);
		}

		if (_beforeSpecialView._winPlace.length)
		{
			if (_beforeSpecialView._winPlace.showCmd == SW_SHOWMAXIMIZED)
			{
				//::ShowWindow(_hSelf, SW_RESTORE);
				::ShowWindow(_hSelf, SW_SHOWMAXIMIZED);
			}
			else
			{
				::SetWindowPlacement(_hSelf, &_beforeSpecialView._winPlace);
			}
		}
		else	//fallback
		{
			::ShowWindow(_hSelf, SW_SHOW);
		}
	}
	//::SetForegroundWindow(_hSelf);
	_beforeSpecialView.isFullScreen = !_beforeSpecialView.isFullScreen;
	::SendMessage(_hSelf, WM_SIZE, 0, 0);
}

void Notepad_plus::postItToggle()
{
	NppParameters * pNppParam = NppParameters::getInstance();
	if (!_beforeSpecialView.isPostIt)	// PostIt disabled, enable it
	{
		NppGUI & nppGUI = (NppGUI &)pNppParam->getNppGUI();
		// get current status before switch to postIt
		//check these always
		{
			_beforeSpecialView.isAlwaysOnTop = ::GetMenuState(_mainMenuHandle, IDM_VIEW_ALWAYSONTOP, MF_BYCOMMAND) == MF_CHECKED;
			_beforeSpecialView.isTabbarShown = ::SendMessage(_hSelf, NPPM_ISTABBARHIDDEN, 0, 0) != TRUE;
			_beforeSpecialView.isStatusbarShown = nppGUI._statusBarShow;
			if (nppGUI._statusBarShow)
				::SendMessage(_hSelf, NPPM_HIDESTATUSBAR, 0, TRUE);
			if (_beforeSpecialView.isTabbarShown)
				::SendMessage(_hSelf, NPPM_HIDETABBAR, 0, TRUE);
			if (!_beforeSpecialView.isAlwaysOnTop)
				::SendMessage(_hSelf, WM_COMMAND, IDM_VIEW_ALWAYSONTOP, 0);
		}
		//Only check these if not fullscreen
        int bs = buttonStatus_postit;
		if (_beforeSpecialView.isFullScreen)
        {
            bs |= buttonStatus_fullscreen; 
        }
        else
		{
			_beforeSpecialView.isMenuShown = ::SendMessage(_hSelf, NPPM_ISMENUHIDDEN, 0, 0) != TRUE;
			if (_beforeSpecialView.isMenuShown)
				::SendMessage(_hSelf, NPPM_HIDEMENU, 0, TRUE);

			//Hide rebar
			_rebarTop.display(false);
			_rebarBottom.display(false);
		}
        _restoreButton.setButtonStatus(bs);

		// PostIt!

		//Set popup style for fullscreen window and store the old style
		if (!_beforeSpecialView.isFullScreen)
		{
			//Hide window so windows can properly update it
			::ShowWindow(_hSelf, SW_HIDE);
			_beforeSpecialView.preStyle = ::SetWindowLongPtr( _hSelf, GWL_STYLE, WS_POPUP );
			if (!_beforeSpecialView.preStyle) {	//something went wrong, use default settings
				_beforeSpecialView.preStyle = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
			}
			//Redraw the window and refresh windowmanager cache, dont do anything else, sizing is done later on
			::SetWindowPos(_hSelf, HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_DRAWFRAME|SWP_FRAMECHANGED);
			::ShowWindow(_hSelf, SW_SHOW);
		}
        
        // show restore button
        _restoreButton.doDialog(_isRTL);

        RECT rect;
        GetWindowRect(_restoreButton.getHSelf(), &rect);
	    int w = rect.right - rect.left;
	    int h = rect.bottom - rect.top;

        RECT nppRect;
        GetWindowRect(_hSelf, &nppRect);
        int x = nppRect.right - w;
        int y = nppRect.top;
        ::MoveWindow(_restoreButton.getHSelf(), x, y, w, h, FALSE);
        
        _pEditView->getFocus();
	}
	else	//PostIt enabled, disable it
	{
        _restoreButton.setButtonStatus(buttonStatus_postit ^ _restoreButton.getButtonStatus());
        _restoreButton.display(false);

		//Setup GUI
		if (!_beforeSpecialView.isFullScreen)
		{
			//only change the these parts of GUI if not already done by fullscreen
			if (_beforeSpecialView.isMenuShown)
				::SendMessage(_hSelf, NPPM_HIDEMENU, 0, FALSE);

			//Show rebar
			_rebarTop.display(true);
			_rebarBottom.display(true);
		}
		//Do this GUI config always
		if (_beforeSpecialView.isStatusbarShown)
			::SendMessage(_hSelf, NPPM_HIDESTATUSBAR, 0, FALSE);
		if (_beforeSpecialView.isTabbarShown)
			::SendMessage(_hSelf, NPPM_HIDETABBAR, 0, FALSE);
		if (!_beforeSpecialView.isAlwaysOnTop)
			::SendMessage(_hSelf, WM_COMMAND, IDM_VIEW_ALWAYSONTOP, 0);

		//restore window style if not fullscreen
		if (!_beforeSpecialView.isFullScreen)
		{
			//dwStyle |= (WS_CAPTION | WS_SIZEBOX);
			::ShowWindow(_hSelf, SW_HIDE);
			::SetWindowLongPtr(_hSelf, GWL_STYLE, _beforeSpecialView.preStyle);
			
			//Redraw the window and refresh windowmanager cache, dont do anything else, sizing is done later on
			::SetWindowPos(_hSelf, HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_DRAWFRAME|SWP_FRAMECHANGED);
			::ShowWindow(_hSelf, SW_SHOW);
		}		
	}

	_beforeSpecialView.isPostIt = !_beforeSpecialView.isPostIt;
	::SendMessage(_hSelf, WM_SIZE, 0, 0);
}

void Notepad_plus::doSynScorll(HWND whichView)
{
	int column = 0;
	int line = 0;
	ScintillaEditView *pView;

	// var for Line
	int mainCurrentLine, subCurrentLine;

	// var for Column
	int mxoffset, sxoffset;
	int pixel;
	int mainColumn, subColumn;

    if (whichView == _mainEditView.getHSelf())
	{	
		if (_syncInfo._isSynScollV)
		{
			// Compute for Line
			mainCurrentLine = _mainEditView.execute(SCI_GETFIRSTVISIBLELINE);
			subCurrentLine = _subEditView.execute(SCI_GETFIRSTVISIBLELINE);
			line = mainCurrentLine - _syncInfo._line - subCurrentLine;
		}
		if (_syncInfo._isSynScollH)
		{
			// Compute for Column
			mxoffset = _mainEditView.execute(SCI_GETXOFFSET);
			pixel = int(_mainEditView.execute(SCI_TEXTWIDTH, STYLE_DEFAULT, (LPARAM)"P"));
			mainColumn = mxoffset/pixel;

			sxoffset = _subEditView.execute(SCI_GETXOFFSET);
			pixel = int(_subEditView.execute(SCI_TEXTWIDTH, STYLE_DEFAULT, (LPARAM)"P"));
			subColumn = sxoffset/pixel;
			column = mainColumn - _syncInfo._column - subColumn;
		}
		pView = &_subEditView;
    }
    else if (whichView == _subEditView.getHSelf())
    {
		if (_syncInfo._isSynScollV)
		{
			// Compute for Line
			mainCurrentLine = _mainEditView.execute(SCI_GETFIRSTVISIBLELINE);
			subCurrentLine = _subEditView.execute(SCI_GETFIRSTVISIBLELINE);
			line = subCurrentLine + _syncInfo._line - mainCurrentLine;
		}
		if (_syncInfo._isSynScollH)
		{
			// Compute for Column
			mxoffset = _mainEditView.execute(SCI_GETXOFFSET);
			pixel = int(_mainEditView.execute(SCI_TEXTWIDTH, STYLE_DEFAULT, (LPARAM)"P"));
			mainColumn = mxoffset/pixel;

			sxoffset = _subEditView.execute(SCI_GETXOFFSET);
			pixel = int(_subEditView.execute(SCI_TEXTWIDTH, STYLE_DEFAULT, (LPARAM)"P"));
			subColumn = sxoffset/pixel;
			column = subColumn + _syncInfo._column - mainColumn;
		}
		pView = &_mainEditView;
    }
    else
        return;

	pView->scroll(column, line);
}

bool Notepad_plus::getIntegralDockingData(tTbData & dockData, int & iCont, bool & isVisible)
{
	DockingManagerData & dockingData = (DockingManagerData &)(NppParameters::getInstance())->getNppGUI()._dockingData;

	for (size_t i = 0 ; i < dockingData._pluginDockInfo.size() ; i++)
	{
		const PlugingDlgDockingInfo & pddi = dockingData._pluginDockInfo[i];

		if (!generic_stricmp(pddi._name.c_str(), dockData.pszModuleName) && (pddi._internalID == dockData.dlgID))
		{
			iCont				= pddi._currContainer;
			isVisible			= pddi._isVisible;
			dockData.iPrevCont	= pddi._prevContainer;

			if (dockData.iPrevCont != -1)
			{
				int cont = (pddi._currContainer < DOCKCONT_MAX ? pddi._prevContainer : pddi._currContainer);
				RECT *pRc = dockingData.getFloatingRCFrom(cont);
				if (pRc)
					dockData.rcFloat	= *pRc;
			}
			return true;
		}
	}
	return false;
}


void Notepad_plus::getCurrentOpenedFiles(Session & session)
{
	_mainEditView.saveCurrentPos();	//save position so itll be correct in the session
	_subEditView.saveCurrentPos();	//both views
	session._activeView = currentView();
	session._activeMainIndex = _mainDocTab.getCurrentTabIndex();
	session._activeSubIndex = _subDocTab.getCurrentTabIndex();

	//Use _invisibleEditView to temporarily open documents to retrieve markers
	//Buffer * mainBuf = _mainEditView.getCurrentBuffer();
	//Buffer * subBuf = _subEditView.getCurrentBuffer();
	Document oldDoc = _invisibleEditView.execute(SCI_GETDOCPOINTER);
	for (int i = 0 ; i < _mainDocTab.nbItem() ; i++)
	{
		BufferID bufID = _mainDocTab.getBufferByIndex(i);
		Buffer * buf = MainFileManager->getBufferByID(bufID);
		if (!buf->isUntitled() && PathFileExists(buf->getFullPathName()))
		{
			generic_string	languageName = getLangFromMenu(buf);
			const TCHAR *langName	= languageName.c_str();

			sessionFileInfo sfi(buf->getFullPathName(), langName, buf->getEncoding(), buf->getPosition(&_mainEditView));

			//_mainEditView.activateBuffer(buf->getID());
			_invisibleEditView.execute(SCI_SETDOCPOINTER, 0, buf->getDocument());
			int maxLine = _invisibleEditView.execute(SCI_GETLINECOUNT);

			for (int j = 0 ; j < maxLine ; j++)
			{
				if ((_invisibleEditView.execute(SCI_MARKERGET, j)&(1 << MARK_BOOKMARK)) != 0)
				{
					sfi.marks.push_back(j);
				}
			}
			session._mainViewFiles.push_back(sfi);
		}
	}

	for (int i = 0 ; i < _subDocTab.nbItem() ; i++)
	{
		BufferID bufID = _subDocTab.getBufferByIndex(i);
		Buffer * buf = MainFileManager->getBufferByID(bufID);
		if (!buf->isUntitled() && PathFileExists(buf->getFullPathName()))
		{
			generic_string	languageName	= getLangFromMenu( buf );
			const TCHAR *langName	= languageName.c_str();

			sessionFileInfo sfi(buf->getFullPathName(), langName, buf->getEncoding(), buf->getPosition(&_subEditView));

			_invisibleEditView.execute(SCI_SETDOCPOINTER, 0, buf->getDocument());
			int maxLine = _invisibleEditView.execute(SCI_GETLINECOUNT);
			for (int j = 0 ; j < maxLine ; j++)
			{
				if ((_invisibleEditView.execute(SCI_MARKERGET, j)&(1 << MARK_BOOKMARK)) != 0)
				{
					sfi.marks.push_back(j);
				}
			}
			session._subViewFiles.push_back(sfi);
		}
	}
	_invisibleEditView.execute(SCI_SETDOCPOINTER, 0, oldDoc);
}

bool Notepad_plus::str2Cliboard(const TCHAR *str2cpy)
{
	if (!str2cpy)
		return false;

	int len2Allocate = lstrlen(str2cpy) + 1;
	len2Allocate *= sizeof(TCHAR);
	unsigned int cilpboardFormat = CF_TEXT;

#ifdef UNICODE
	cilpboardFormat = CF_UNICODETEXT;
#endif

	HGLOBAL hglbCopy = ::GlobalAlloc(GMEM_MOVEABLE, len2Allocate);
	if (hglbCopy == NULL) 
	{ 
		return false; 
	} 
	
	if (!::OpenClipboard(_hSelf)) 
		return false; 
		
	::EmptyClipboard();

	// Lock the handle and copy the text to the buffer. 
	TCHAR *pStr = (TCHAR *)::GlobalLock(hglbCopy);
	lstrcpy(pStr, str2cpy);
	::GlobalUnlock(hglbCopy); 

	// Place the handle on the clipboard. 
	::SetClipboardData(cilpboardFormat, hglbCopy);
	::CloseClipboard();
	return true;
}

//ONLY CALL IN CASE OF EMERGENCY: EXCEPTION
//This function is destructive
bool Notepad_plus::emergency(generic_string emergencySavedDir) 
{
    ::CreateDirectory(emergencySavedDir.c_str(), NULL);
	return dumpFiles(emergencySavedDir.c_str(), TEXT("File"));
}

bool Notepad_plus::dumpFiles(const TCHAR * outdir, const TCHAR * fileprefix) {
	//start dumping unsaved files to recovery directory
	bool somethingsaved = false;
	bool somedirty = false;
	TCHAR savePath[MAX_PATH] = {0};

	//rescue primary
	for(int i = 0; i < MainFileManager->getNrBuffers(); i++) {
		Buffer * docbuf = MainFileManager->getBufferByIndex(i);
		if (!docbuf->isDirty())	//skip saved documents
			continue;
		else
			somedirty = true;

		const TCHAR * unitext = (docbuf->getUnicodeMode() != uni8Bit)?TEXT("_utf8"):TEXT("");
		wsprintf(savePath, TEXT("%s\\%s%03d%s.dump"), outdir, fileprefix, i, unitext);

		bool res = MainFileManager->saveBuffer(docbuf->getID(), savePath);

		somethingsaved |= res;
	}

	return somethingsaved || !somedirty;
}

void Notepad_plus::drawTabbarColoursFromStylerArray()
{
	Style *stActText = getStyleFromName(TABBAR_ACTIVETEXT);
	if (stActText && stActText->_fgColor != -1)
		TabBarPlus::setColour(stActText->_fgColor, TabBarPlus::activeText);

	Style *stActfocusTop = getStyleFromName(TABBAR_ACTIVEFOCUSEDINDCATOR);
	if (stActfocusTop && stActfocusTop->_fgColor != -1)
		TabBarPlus::setColour(stActfocusTop->_fgColor, TabBarPlus::activeFocusedTop);

	Style *stActunfocusTop = getStyleFromName(TABBAR_ACTIVEUNFOCUSEDINDCATOR);
	if (stActunfocusTop && stActunfocusTop->_fgColor != -1)
		TabBarPlus::setColour(stActunfocusTop->_fgColor, TabBarPlus::activeUnfocusedTop);

	Style *stInact = getStyleFromName(TABBAR_INACTIVETEXT);
	if (stInact && stInact->_fgColor != -1)
		TabBarPlus::setColour(stInact->_fgColor, TabBarPlus::inactiveText);
	if (stInact && stInact->_bgColor != -1)
		TabBarPlus::setColour(stInact->_bgColor, TabBarPlus::inactiveBg);
}

void Notepad_plus::notifyBufferChanged(Buffer * buffer, int mask) {
	NppParameters *pNppParam = NppParameters::getInstance();
	const NppGUI & nppGUI = pNppParam->getNppGUI();

	_mainEditView.bufferUpdated(buffer, mask);
	_subEditView.bufferUpdated(buffer, mask);
	_mainDocTab.bufferUpdated(buffer, mask);
	_subDocTab.bufferUpdated(buffer, mask);

	bool mainActive = (_mainEditView.getCurrentBuffer() == buffer);
	bool subActive = (_subEditView.getCurrentBuffer() == buffer);

	//Only event that applies to non-active Buffers
	if (mask & BufferChangeStatus) {	//reload etc
		bool didDialog = false;
		switch(buffer->getStatus()) {
			case DOC_UNNAMED: 	//nothing todo
			{
				break;
			}
			case DOC_REGULAR: 	//nothing todo
			{
				break; 
			}
			case DOC_MODIFIED:	//ask for reloading
			{
				bool autoUpdate = (nppGUI._fileAutoDetection == cdAutoUpdate) || (nppGUI._fileAutoDetection == cdAutoUpdateGo2end);
				if (!autoUpdate || buffer->isDirty())
				{
					didDialog = true;
					if (doReloadOrNot(buffer->getFullPathName(), buffer->isDirty()) != IDYES)
						break;	//abort
				}
				//activateBuffer(buffer->getID(), iView);	//activate the buffer in the first view possible
				doReload(buffer->getID(), false);
				if (mainActive || subActive)
				{
					performPostReload(mainActive?MAIN_VIEW:SUB_VIEW);
				}
				break;
			}
			case DOC_DELETED: 	//ask for keep
			{
				int index = _pDocTab->getIndexByBuffer(buffer->getID());
				int iView = currentView();
				if (index == -1)
					iView = otherView();
				//activateBuffer(buffer->getID(), iView);	//activate the buffer in the first view possible
				didDialog = true;
				if (doCloseOrNot(buffer->getFullPathName()) == IDNO)
				{
					//close in both views, doing current view last since that has to remain opened
					doClose(buffer->getID(), otherView());
					doClose(buffer->getID(), currentView());
				}
				break;
			}
		}

		if (didDialog)
		{
			int curPos = _pEditView->execute(SCI_GETCURRENTPOS);
			::PostMessage(_pEditView->getHSelf(), WM_LBUTTONUP, 0, 0);
			::PostMessage(_pEditView->getHSelf(), SCI_SETSEL, curPos, curPos);
			if (::IsIconic(_hSelf))
				::ShowWindow(_hSelf, SW_RESTORE);
		}
	}

    if (mask & (BufferChangeReadonly))
	{
		checkDocState();
		
		bool isSysReadOnly = buffer->getFileReadOnly();
		bool isUserReadOnly = buffer->getUserReadOnly();
		bool isDirty = buffer->isDirty();

		// To notify plugins ro status is changed
		SCNotification scnN;
		scnN.nmhdr.hwndFrom = (void *)buffer->getID();
		scnN.nmhdr.idFrom = (uptr_t)  ((isSysReadOnly || isUserReadOnly? DOCSTAUS_READONLY : 0) | (isDirty ? DOCSTAUS_BUFFERDIRTY : 0));
		scnN.nmhdr.code = NPPN_READONLYCHANGED;
		_pluginsManager.notify(&scnN);

	}

	if (!mainActive && !subActive)
	{
		return;
	}

	if (mask & (BufferChangeLanguage))
	{
		if (mainActive)
			_autoCompleteMain.setLanguage(buffer->getLangType());
		if (subActive)
			_autoCompleteSub.setLanguage(buffer->getLangType());
	}

	if ((currentView() == MAIN_VIEW) && !mainActive)
		return;

	if ((currentView() == SUB_VIEW) && !subActive)
		return;

	if (mask & (BufferChangeDirty|BufferChangeFilename)) 
	{
		checkDocState();
		setTitle();
		generic_string dir(buffer->getFullPathName());
		PathRemoveFileSpec(dir);
		setWorkingDir(dir.c_str());
	}

	if (mask & (BufferChangeLanguage)) 
	{
		checkLangsMenu(-1);	//let N++ do search for the item
		setLangStatus(buffer->getLangType());
		if (_mainEditView.getCurrentBuffer() == buffer)
			_autoCompleteMain.setLanguage(buffer->getLangType());
		else if (_subEditView.getCurrentBuffer() == buffer)
			_autoCompleteSub.setLanguage(buffer->getLangType());
		
		SCNotification scnN;
		scnN.nmhdr.code = NPPN_LANGCHANGED;
		scnN.nmhdr.hwndFrom = _hSelf;
		scnN.nmhdr.idFrom = (uptr_t)_pEditView->getCurrentBufferID();
		_pluginsManager.notify(&scnN);
	}

	if (mask & (BufferChangeFormat|BufferChangeLanguage|BufferChangeUnicode))
	{
		updateStatusBar();
		checkUnicodeMenuItems(/*buffer->getUnicodeMode()*/);
		setUniModeText();
		setDisplayFormat(buffer->getFormat());
		enableConvertMenuItems(buffer->getFormat());
	}
}

void Notepad_plus::notifyBufferActivated(BufferID bufid, int view) {
	Buffer * buf = MainFileManager->getBufferByID(bufid);
	buf->increaseRecentTag();
	
	if (view == MAIN_VIEW) {
		_autoCompleteMain.setLanguage(buf->getLangType());
	} else if (view == SUB_VIEW) {
		_autoCompleteSub.setLanguage(buf->getLangType());
	}

	if (view != currentView()) {
		return;	//dont care if another view did something
	}

	checkDocState();
	dynamicCheckMenuAndTB();
	setLangStatus(buf->getLangType());
	updateStatusBar();
	checkUnicodeMenuItems(/*buf->getUnicodeMode()*/);
	setUniModeText();
	setDisplayFormat(buf->getFormat());
	enableConvertMenuItems(buf->getFormat());
	generic_string dir(buf->getFullPathName());
	PathRemoveFileSpec(dir);
	setWorkingDir(dir.c_str());
	setTitle();
	//Make sure the colors of the tab controls match
	::InvalidateRect(_mainDocTab.getHSelf(), NULL, FALSE);
	::InvalidateRect(_subDocTab.getHSelf(), NULL, FALSE);

	SCNotification scnN;
	scnN.nmhdr.code = NPPN_BUFFERACTIVATED;
	scnN.nmhdr.hwndFrom = _hSelf;
	scnN.nmhdr.idFrom = (uptr_t)bufid;
	_pluginsManager.notify(&scnN);

	_linkTriggered = true;
}

void Notepad_plus::loadCommandlineParams(const TCHAR * commandLine, CmdLineParams * pCmdParams) {
	if (!commandLine || ! pCmdParams)
		return;

	FileNameStringSplitter fnss(commandLine);
	const TCHAR *pFn = NULL;
			
 	LangType lt = pCmdParams->_langType;//LangType(pCopyData->dwData & LASTBYTEMASK);
	int ln =  pCmdParams->_line2go;
    int cn = pCmdParams->_column2go;

	bool readOnly = pCmdParams->_isReadOnly;

	BufferID lastOpened = BUFFER_INVALID;
	for (int i = 0 ; i < fnss.size() ; i++)
	{
		pFn = fnss.getFileName(i);
		BufferID bufID = doOpen(pFn, readOnly);
		if (bufID == BUFFER_INVALID)	//cannot open file
			continue;

		lastOpened = bufID;

		if (lt != L_EXTERNAL && lt < NppParameters::getInstance()->L_END)
		{
			Buffer * pBuf = MainFileManager->getBufferByID(bufID);
			pBuf->setLangType(lt);
		}

		if (ln != -1) 
		{	//we have to move the cursor manually
			int iView = currentView();	//store view since fileswitch can cause it to change
			switchToFile(bufID);	//switch to the file. No deferred loading, but this way we can easily move the cursor to the right position

            if (cn == -1)
			_pEditView->execute(SCI_GOTOLINE, ln-1);
            else
            {
                int pos = _pEditView->execute(SCI_FINDCOLUMN, ln-1, cn-1);
                _pEditView->execute(SCI_GOTOPOS, pos);
            }
			switchEditViewTo(iView);	//restore view
		}
	}
	if (lastOpened != BUFFER_INVALID)
    {
		switchToFile(lastOpened);
	}
}

void Notepad_plus::setFindReplaceFolderFilter(const TCHAR *dir, const TCHAR *filter)
{
	generic_string fltr;
	NppParameters *pNppParam = NppParameters::getInstance();
	FindHistory & findHistory = pNppParam->getFindHistory();

	// get current directory in case it's not provided.
	if (!dir && findHistory._isFolderFollowDoc)
	{
		dir = pNppParam->getWorkingDir();
	}

	// get current language file extensions in case it's not provided.
	if (!filter && findHistory._isFilterFollowDoc)
	{
		// Get current language file extensions
		const TCHAR *ext = NULL;
		LangType lt = _pEditView->getCurrentBuffer()->getLangType();

		if (lt == L_USER)
		{
			Buffer * buf = _pEditView->getCurrentBuffer();
			UserLangContainer * userLangContainer = pNppParam->getULCFromName(buf->getUserDefineLangName());
			if (userLangContainer) 
				ext = userLangContainer->getExtention();
		}
		else
		{
			ext = NppParameters::getInstance()->getLangExtFromLangType(lt);
		}

		if (ext && ext[0])
		{
			fltr = TEXT("");
			vector<generic_string> vStr;
			cutString(ext, vStr);
			for (size_t i = 0; i < vStr.size(); i++)
			{
				fltr += TEXT("*.");
				fltr += vStr[i] + TEXT(" ");
			}
		}
		else
		{
			fltr = TEXT("*.*");
		}
		filter = fltr.c_str();
	}
	_findReplaceDlg.setFindInFilesDirFilter(dir, filter);
}

vector<generic_string> Notepad_plus::addNppComponents(const TCHAR *destDir, const TCHAR *extFilterName, const TCHAR *extFilter)
{
    FileDialog fDlg(_hSelf, _hInst);
    fDlg.setExtFilter(extFilterName, extFilter, NULL);
	
    vector<generic_string> copiedFiles;

    if (stringVector *pfns = fDlg.doOpenMultiFilesDlg())
    {
        // Get plugins dir
		generic_string destDirName = (NppParameters::getInstance())->getNppPath();
        PathAppend(destDirName, destDir);

        if (!::PathFileExists(destDirName.c_str()))
        {
            ::CreateDirectory(destDirName.c_str(), NULL);
        }

        destDirName += TEXT("\\");
        
        size_t sz = pfns->size();
        for (size_t i = 0 ; i < sz ; i++) 
        {
            if (::PathFileExists(pfns->at(i).c_str()))
            {
                // copy to plugins directory
                generic_string destName = destDirName;
                destName += ::PathFindFileName(pfns->at(i).c_str());
                //printStr(destName.c_str());
                if (::CopyFile(pfns->at(i).c_str(), destName.c_str(), FALSE))
                    copiedFiles.push_back(destName.c_str());
            }
        }
    }
    return copiedFiles;
}

void Notepad_plus::setWorkingDir(const TCHAR *dir) 
{
	NppParameters * params = NppParameters::getInstance();
	if (params->getNppGUI()._openSaveDir == dir_last)
		return;
	if (params->getNppGUI()._openSaveDir == dir_userDef)
	{
		params->setWorkingDir(NULL);
	}
	else if (dir && PathIsDirectory(dir))
	{
		params->setWorkingDir(dir);
	}
}

int Notepad_plus::getLangFromMenuName(const TCHAR * langName)
{
	int	id	= 0;
	const int menuSize = 64;
	TCHAR menuLangName[menuSize];

	for ( int i = IDM_LANG_C; i <= IDM_LANG_USER; i++ )
		if ( ::GetMenuString( _mainMenuHandle, i, menuLangName, menuSize, MF_BYCOMMAND ) )
			if ( !lstrcmp( langName, menuLangName ) )
			{
				id	= i;
				break;
			}

	if ( id == 0 )
	{
		for ( int i = IDM_LANG_USER + 1; i <= IDM_LANG_USER_LIMIT; i++ )
			if ( ::GetMenuString( _mainMenuHandle, i, menuLangName, menuSize, MF_BYCOMMAND ) )
				if ( !lstrcmp( langName, menuLangName ) )
				{
					id	= i;
					break;
				}
	}

	return id;
}

generic_string Notepad_plus::getLangFromMenu(const Buffer * buf) 
{
	
	int	id;
	generic_string userLangName;
	const int nbChar = 32;
	TCHAR menuLangName[nbChar];

	id = (NppParameters::getInstance())->langTypeToCommandID( buf->getLangType() );
	if ( ( id != IDM_LANG_USER ) || !( buf->isUserDefineLangExt() ) )
	{
		::GetMenuString(_mainMenuHandle, id, menuLangName, nbChar-1, MF_BYCOMMAND);
		userLangName = menuLangName;
	}
	else
	{
		userLangName = buf->getUserDefineLangName();
	}
	return	userLangName;
}

Style * Notepad_plus::getStyleFromName(const TCHAR *styleName)
{
	StyleArray & stylers = (NppParameters::getInstance())->getMiscStylerArray();

	int i = stylers.getStylerIndexByName(styleName);
	Style * st = NULL;
	if (i != -1)
	{
		Style & style = stylers.getStyler(i);
		st = &style;
	}
	return st;
}

bool Notepad_plus::noOpenedDoc() const
{
	if (_mainDocTab.isVisible() && _subDocTab.isVisible())
		return false;
	if (_pDocTab->nbItem() == 1)
	{
		BufferID buffer = _pDocTab->getBufferByIndex(0);
		Buffer * buf = MainFileManager->getBufferByID(buffer);
		if (!buf->isDirty() && buf->isUntitled())
			return true;
	}
	return false;
}