
#include "bg_import.h"

#include "bg_bashAPI.h"

int importManifestCriteria(ManifestRecord* rec, ManifestRecord* target)
{
    // remove any leading paths
    char* target_assetName = target->assetName;
    for (char* t = target_assetName; t && *t; t++)
        if (*t == '/' && *(t+1)!='\0')
            target_assetName = t+1;

    // find the last '.' or '\0' if there is none
    char* ext = target_assetName + strlen(target_assetName);
    for (char* t = ext; t && *t; t++)
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
    SHELL_VAR* vImportedLibraries = ShellVar_find("_importedLibraries");
    if (!vImportedLibraries) {
        vImportedLibraries = ShellVar_assocCreateGlobal("vImportedLibraries");
    }
    SHELL_VAR* vL1 = ShellVar_findOrCreate("L1");
    SHELL_VAR* vL2 = ShellVar_findOrCreate("L2");
    ShellVar_set(vL1,"");
    ShellVar_set(vL2,"");
    char* lookupName = save2string("lib:",scriptName);
    if (flags&im_forceFlag==0 && flags&im_getPathFlag==0 && strcmp(ShellVar_assocGet(vImportedLibraries, lookupName),"")!=0) {
        return EXECUTION_SUCCESS;
    }

    ManifestRecord targetRec;
    ManifestRecord foundManRec = manifestGet(NULL, NULL, ManifestRecord_assign(&targetRec, scriptName,NULL,NULL,NULL), importManifestCriteria);
    if (!foundManRec.assetPath) {
        // TODO: the bash import function falls back to searching the bgVinstalledPaths ENV var and /usr/lib/ when its not found in the manifest
        //       but that might be phased out so not sure if we ever need to implement it is C. It is usefull during development, though if updating the manifest is not perfect.
        ShellVar_set(vL1,"assertError import could not find bash script library in host manifest");
        xfree(lookupName);
        return 0;
    }

    // if we are only asked to get the path, do that and return
    if (retVar && *retVar) {
        *retVar = savestring(foundManRec.assetPath);
        xfree(foundManRec.assetPath);
        xfree(lookupName);
        return 1;
    }

    // TODO: SECURITY:  the bash import function checks if [ "$bgSourceOnlyUnchangable" ] && [ -w "$foundManRec.assetPath" ]

    ShellVar_assocSet(vImportedLibraries, lookupName, foundManRec.assetPath);


    ManifestRecord_free(&foundManRec);
    xfree(lookupName);
    return 0;
}
