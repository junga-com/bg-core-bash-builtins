
#include "bg_import.h"

#include "bg_bashAPI.h"
#include "BGString.h"
#include "bg_objects.h"


int importManifestCriteria(ManifestRecord* rec, ManifestRecord* target)
{
    if (!target->assetName)
        return 0;

    // remove any leading paths
    char* target_assetName = target->assetName;
    for (char* t = target_assetName; t && *t; t++)
        if (*t == '/' && *(t+1)!='\0')
            target_assetName = t+1;

    // find the last '.' or '\0' if there is none
    char* ext = target_assetName + strlen(target_assetName);
    for (char* t = target_assetName; t && *t; t++)
        if (*t == '.')
            ext = t;

    // assetName is without path and without ext
    char* target_assetNameWOExt = savestringn(target_assetName, ext-target_assetName);

    // remove any leading paths
    char* rec_pathWOFolders = rec->assetPath;
    for (char* t = rec_pathWOFolders; t && *t; t++)
        if (*t == '/' && *(t+1)!='\0')
            rec_pathWOFolders = t+1;

    int matched = 0;
    if (strncmp(rec->assetType, "lib.script.bash", 15)==0 && (strcmp(rec->assetName,target_assetNameWOExt)==0 || strcmp(rec->assetName,target_assetName)==0) )
        matched = 1;
    if (!matched && strcmp(rec_pathWOFolders,target_assetName)==0 && (strncmp(rec->assetType, "plugin", 15)==0 || strncmp(rec->assetType, "unitTest", 15)==0))
        matched = 1;
    xfree(target_assetNameWOExt);
    return matched;
}

int importBashLibrary(char* scriptName, int flags, char** retVar)
{
    bgtrace0(0,"STR importBashLibrary\n");
    SHELL_VAR* vImportedLibraries = ShellVar_find("_importedLibraries");
    if (!vImportedLibraries) {
        vImportedLibraries = ShellVar_assocCreateGlobal("vImportedLibraries");
    }

    // even though this builtin implementation does not need L1 and L2, we must ensure that they are empty because the script
    // will call $L1;$L2 because it does not know which implementation will be called.
    SHELL_VAR* vL1 = ShellVar_findOrCreate("L1");
    SHELL_VAR* vL2 = ShellVar_findOrCreate("L2");
    ShellVar_set(vL1,"");
    ShellVar_set(vL2,"");

    // lookupName is the index used in the vImportedLibraries cache
    char* lookupName = save2string("lib:",scriptName);

    // return quickly when the library is already loaded
    if (flags&im_forceFlag==0 && flags&im_getPathFlag==0 && strcmp(ShellVar_assocGet(vImportedLibraries, lookupName),"")!=0) {
        return EXECUTION_SUCCESS;
    }

    // find the library in the manifest or in the system paths
    ManifestRecord targetRec;
    ManifestRecord foundManRec = manifestGet(NULL, NULL, ManifestRecord_assign(&targetRec, NULL,NULL,scriptName,NULL), importManifestCriteria);
    if (!foundManRec.assetPath) {
        // TODO: the bash import function falls back to searching the bgVinstalledPaths ENV var and /usr/lib/ when its not found in the manifest
        //       but that might be phased out so not sure if we ever need to implement it is C. It is usefull during development, though if updating the manifest is not perfect.
        ShellVar_set(vL1,"assertError import could not find bash script library in host manifest");
        xfree(lookupName);
        return 0;
    }

    // if the caller passed in a var to receive the path, fill it in
    if (retVar) {
        *retVar = savestring(foundManRec.assetPath);

        // if we are only asked to get the path, return
        if (flags&im_getPathFlag) {
            ManifestRecord_free(&foundManRec);
            xfree(lookupName);
            return 1;
        }
    }

    // TODO: SECURITY:  the bash import function checks if [ "$bgSourceOnlyUnchangable" ] && [ -w "$foundManRec.assetPath" ]

    // record that this scriptName has been sourced (do it first to avoid recursive loop)
    ShellVar_assocSet(vImportedLibraries, lookupName, foundManRec.assetPath);

    // now call the 'source' builtin to do the work
    WORD_LIST* args = NULL;
    args = make_word_list(make_word(foundManRec.assetPath), args);

    int errexitValue = minus_o_option_value("errexit");
    if (flags & im_stopOnErrorFlag)
        set_minus_o_option(1, "errexit");

    int ret = source_builtin(args);

    if (flags & im_stopOnErrorFlag)
        set_minus_o_option(errexitValue, "errexit");

    bgtrace1(0,"source_builtin returned '%d'\n",ret);

    // do FlushPendingClassConstructions
    BGString pendingClassCtors;
    BGString_initFromStr(&pendingClassCtors, ShellVar_assocGet(ShellVar_find("Class"), "pendingClassCtors"));
    BGString_replaceWhitespaceWithNulls(&pendingClassCtors);
    char* pendingClass;
    while (pendingClass = BGString_nextWord(&pendingClassCtors)) {
        DeclareClassEnd(pendingClass);
    }
    BGString_free(&pendingClassCtors);

    dispose_words(args);
    ManifestRecord_free(&foundManRec);
    xfree(lookupName);
    return ret;
}
