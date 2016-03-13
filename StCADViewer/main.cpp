/**
 * This source is a part of sView program.
 *
 * Copyright © Kirill Gavrilov, 2016
 */

#ifndef __APPLE__

#include <StVersion.h>
#include <StFile/StFolder.h>
#include <StStrings/stConsole.h>

#include "../StOutPageFlip/StOutPageFlip.h"
#include "../StCADViewer/StCADViewer.h"

static StString getAbout() {
    StString anAboutString =
        StString("StCADViewer ") + StVersionInfo::getSDKVersionString() + '\n'
        + "Copyright (C) 2007-2016 Kirill Gavrilov (kirill@sview.ru).\n"
        + "Usage: StCADViewer [options] - file\n";
    return anAboutString;
}

#ifdef _WIN32
#ifdef ST_DEBUG
int main(int , char** ) { // force console output
#else
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { // prevent console output
#endif
    setlocale(LC_ALL, ".OCP"); // we set default locale for console output (useful only for debug)
#else
int main(int , char** ) {
#endif
    StOutPageFlip::initGlobalsAsync();
    if(!StVersionInfo::checkTimeBomb("sView")) {
        return 1;
    }

    // setup environment variables
    const StString ST_ENV_NAME_STCORE_PATH =
    #if defined(_WIN64) || defined(_LP64) || defined(__LP64__)
        "StCore64";
    #else
        "StCore32";
    #endif
    const StString aProcessPath = StProcess::getProcessFolder();
    StString aProcessUpPath = StFileNode::getFolderUp(aProcessPath);
    if(!aProcessUpPath.isEmpty()) {
        aProcessUpPath += SYS_FS_SPLITTER;
    }
    StProcess::setEnv(ST_ENV_NAME_STCORE_PATH, aProcessPath);
    if(StFolder::isFolder(aProcessPath + "textures")) {
        StProcess::setEnv("StShare", aProcessPath);
    } else if(StFolder::isFolder(aProcessUpPath + "textures")) {
        StProcess::setEnv("StShare", aProcessUpPath);
    }

    StHandle<StOpenInfo> anInfo;
    if(anInfo.isNull()
    || (!anInfo->hasPath() && !anInfo->hasArgs())) {
        anInfo = StApplication::parseProcessArguments();
    }
    if(anInfo.isNull()) {
        // show help
        StString aShowHelpString = getAbout();
        st::cout << aShowHelpString;
        stInfo(aShowHelpString);
        return NULL;
    }

    StHandle<StResourceManager> aResMgr = new StResourceManager();
    StHandle<StCADViewer> anApp  = new StCADViewer(aResMgr, NULL, anInfo);
    if(!anApp->open()) {
        return 1;
    }
    return anApp->exec();
}

#endif // __APPLE__