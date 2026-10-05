cmake_minimum_required(VERSION 3.16)

add_executable(organizer)
set_target_properties(organizer PROPERTIES OUTPUT_NAME "ModOrganizer")
mo2_configure_executable(organizer
	WARNINGS OFF
	EXTRA_TRANSLATIONS ${MO2_SUPER_PATH}/game_gamebryo/src ${MO2_UIBASE_PATH}/src
	PRIVATE_DEPENDS
		uibase githubpp bsatk esptk archive usvfs lootcli boost::program_options
		DirectXTex libbsarch Qt::WebEngineWidgets Qt::WebSockets)

# Allow the fork release build to use an official, newer USVFS package without
# replacing the dependency cache used by other MO2 builds on this machine.
set(MO2_USVFS_OVERRIDE_ROOT "" CACHE PATH "Optional USVFS release package root")
if(NOT MO2_USVFS_OVERRIDE_ROOT STREQUAL "")
	if(NOT EXISTS "${MO2_USVFS_OVERRIDE_ROOT}/bin/usvfs_x64.dll" OR
	   NOT EXISTS "${MO2_USVFS_OVERRIDE_ROOT}/lib/usvfs_x64.lib" OR
	   NOT EXISTS "${MO2_USVFS_OVERRIDE_ROOT}/include/usvfs/usvfs.h")
		message(FATAL_ERROR "MO2_USVFS_OVERRIDE_ROOT does not contain a complete USVFS package")
	endif()
	set_target_properties(mo2-usvfs PROPERTIES
		IMPORTED_LOCATION "${MO2_USVFS_OVERRIDE_ROOT}/bin/usvfs_x64.dll"
		IMPORTED_IMPLIB "${MO2_USVFS_OVERRIDE_ROOT}/lib/usvfs_x64.lib"
		INTERFACE_INCLUDE_DIRECTORIES "${MO2_USVFS_OVERRIDE_ROOT}/include/usvfs")
endif()

target_link_libraries(organizer PUBLIC Shlwapi)
if(WIN32)
	target_link_libraries(organizer PRIVATE Dwmapi)
endif()
target_include_directories(organizer PUBLIC ${DDS_ROOT})
mo2_install_target(organizer)

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/dlls.manifest.qt6"
		DESTINATION bin/dlls
		CONFIGURATIONS Release RelWithDebInfo
		RENAME dlls.manifest)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/dlls.manifest.debug.qt6"
		DESTINATION bin/dlls
		CONFIGURATIONS Debug
		RENAME dlls.manifest)

install(FILES
	"${CMAKE_CURRENT_SOURCE_DIR}/../../revamped/themes/stylesheets/MO2 Classic Refined - Light.qss"
	DESTINATION bin/stylesheets
	RENAME Light.qss)
install(FILES
	"${CMAKE_CURRENT_SOURCE_DIR}/../../revamped/themes/stylesheets/MO2 Classic Refined - Dark.qss"
	DESTINATION bin/stylesheets
	RENAME dark.qss)
install(DIRECTORY
	"${CMAKE_CURRENT_SOURCE_DIR}/../../revamped/themes/stylesheets/mo2-classic-refined-light"
	DESTINATION bin/stylesheets)
install(DIRECTORY
	"${CMAKE_CURRENT_SOURCE_DIR}/tutorials"
	DESTINATION bin)

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/markdown.html"
	DESTINATION bin/resources)

mo2_deploy_qt(BINARIES ModOrganizer.exe uibase.dll plugins/bsa_packer.dll)

mo2_add_filter(NAME src/application GROUPS
	iuserinterface
	commandline
	main
	moapplication
	moshortcut
	multiprocess
	sanitychecks
	selfupdater
	updatedialog
)

mo2_add_filter(NAME src/browser GROUPS
	browserdialog
	browserview
)

mo2_add_filter(NAME src/categories GROUPS
        categories
	categoriestable
        categoriesdialog
	categoryimportdialog
)

mo2_add_filter(NAME src/core GROUPS
	archivefiletree
	installationmanager
	nexusinterface
	nxmaccessmanager
	organizercore
	game_features
	plugincontainer
	apiuseraccount
	processrunner
	qdirfiletree
	virtualfiletree
	uilocker
)

mo2_add_filter(NAME src/dialogs GROUPS
	aboutdialog
	activatemodsdialog
	credentialsdialog
	filedialogmemory
	forcedloaddialog
	forcedloaddialogwidget
	listdialog
	messagedialog
	motddialog
	overwriteinfodialog
	queryoverwritedialog
	problemsdialog
	savetextasdialog
	selectiondialog
	syncoverwritedialog
	transfersavesdialog
	waitingonclosedialog
)

mo2_add_filter(NAME src/downloads GROUPS
	downloadlist
	downloadlistview
	downloadmanager
)

mo2_add_filter(NAME src/env GROUPS
	env
	envdump
	envfs
	envmetrics
	envmodule
	envsecurity
	envshell
	envshortcut
	envwindows
)

mo2_add_filter(NAME src/executables GROUPS
	executableslist
	editexecutablesdialog
)

mo2_add_filter(NAME src/instances GROUPS
	createinstancedialog
	createinstancedialogpages
	instancemanager
	instancemanagerdialog
)

mo2_add_filter(NAME src/loot GROUPS
	loot
	lootdialog
)

mo2_add_filter(NAME src/mainwindow GROUPS
	datatab
	downloadstab
	iconfetcher
	filetree
	filetreeitem
	filetreemodel
	mainwindow
	savestab
	statusbar
)

mo2_add_filter(NAME src/modinfo GROUPS
	modinfo
	modinfobackup
	modinfoforeign
	modinfooverwrite
	modinforegular
	modinfoseparator
	modinfowithconflictinfo
)

mo2_add_filter(NAME src/modinfo/dialog GROUPS
	modinfodialog
	modinfodialogcategories
	modinfodialogconflicts
	modinfodialogconflictsmodels
	modinfodialogesps
	modinfodialogfiletree
	modinfodialogfwd
	modinfodialogimages
	modinfodialognexus
	modinfodialogtab
	modinfodialogtextfiles
)

mo2_add_filter(NAME src/modinfo/dialog/widgets GROUPS
	modidlineedit
)

mo2_add_filter(NAME src/modlist GROUPS
	modlist
	modlistdropinfo
	modlistsortproxy
	modlistbypriorityproxy
)

mo2_add_filter(NAME src/modlist/view GROUPS
	filterlist
	modlistview
	modlistviewactions
	modlistcontextmenu
	modflagicondelegate
	modcontenticondelegate
	modconflicticondelegate
	modlistversiondelegate
)

mo2_add_filter(NAME src/plugins GROUPS
	pluginlist
	pluginlistsortproxy
	pluginlistview
	pluginlistcontextmenu
)

mo2_add_filter(NAME src/previews GROUPS
	previewdialog
	previewgenerator
)

mo2_add_filter(NAME src/profiles GROUPS
	eldenringsavesettings
	profile
	profileinputdialog
	profilesdialog
)

mo2_add_filter(NAME src/proxies GROUPS
	downloadmanagerproxy
	gamefeaturesproxy
	modlistproxy
	organizerproxy
	pluginlistproxy
	proxyutils
)

mo2_add_filter(NAME src/register GROUPS
	shared/directoryentry
	shared/fileentry
	shared/filesorigin
	shared/fileregister
	shared/fileregisterfwd
	shared/originconnection
	directoryrefresher
)

mo2_add_filter(NAME src/settings GROUPS
	settings
	settingsutilities
)

mo2_add_filter(NAME src/settingsdialog GROUPS
	settingsdialog
	settingsdialogdiagnostics
	settingsdialoggeneral
	settingsdialognexus
	settingsdialogpaths
	settingsdialogplugins
	settingsdialogworkarounds
	settingsdialogmodlist
	settingsdialogtheme
	disableproxyplugindialog
)

mo2_add_filter(NAME src/utilities GROUPS
	shared/appconfig
	bbcode
	csvbuilder
	persistentcookiejar
	serverinfo
	spawn
	shared/util
	usvfsconnector
	shared/windows_error
	thread_utils
	json
	glob_matching
)

mo2_add_filter(NAME src/widgets GROUPS
	colortable
	genericicondelegate
	filerenamer
	icondelegate
	lcdnumber
	loglist
	loghighlighter
	noeditdelegate
	qtgroupingproxy
	texteditor
	viewmarkingscrollbar
	modelutils
	copyeventfilter
)
