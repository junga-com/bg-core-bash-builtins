
#include "bg_import.h"

#include <stdc.h>
#include <flags.h>

#include "bg_bashAPI.h"
#include "BGString.h"
#include "bg_objects.h"



int importBashLibrary(char* scriptName, int flags, char** retVar)
{
	SHELL_VAR* vImportedLibraries = ShellVar_find("_importedLibraries");
	if (!vImportedLibraries) {
		vImportedLibraries = ShellVar_assocCreateGlobal("_importedLibraries");
	}

	// even though this builtin implementation does not need L1 and L2, we must ensure that they are empty because the script
	// will call $L1;$L2 because it does not know which implementation will be called.
	SHELL_VAR* vL1 = ShellVar_createSet("L1","");
	SHELL_VAR* vL2 = ShellVar_createSet("L2","");

	// lookupName is the index used in the vImportedLibraries cache
	char* lookupName = save2string("lib:",scriptName);

	// return quickly when the library is already loaded
	char* loadedPath = ShellVar_assocGet(vImportedLibraries, lookupName);
	if (loadedPath!=NULL && (flags&im_forceFlag)==0 && (flags&im_getPathFlag)==0) {
		xfree(lookupName);
		return EXECUTION_SUCCESS;
	}

	bgtrace1(1,"STR importBashLibrary(%s)\n",scriptName); bgtracePush();

	ManifestRecord foundManRec = {0};


	ManifestRecord targetRec; ManifestRecord_makeImportCriteria(&targetRec, scriptName);
	foundManRec = manifestGetOne(NULL, NULL, &targetRec, NULL);

	// __bgtrace("!!! builtin import: \n");
	// ManifestRecord_bgtrace(&targetRec, "!!! targetRec");
	// ManifestRecord_bgtrace(&foundManRec, "!!! foundManRec");
	//exit(1);

	if (foundManRec.assetPath && !fsExists(foundManRec.assetPath)) {
		xfree(lookupName);
		ManifestRecord_cleanup(&foundManRec);
		return assertError(NULL,"import: path found in manifest for '%s' does not exist. Ussually this means the hostmanifest file needs to be rebuilt.", scriptName);
	}

	if (!foundManRec.assetPath) {
		if (!(flags&im_devOnlyFlag))
			__bgtrace("import searching paths for '%s'\n",scriptName);

		bgtrace0(2,"not found in manifest ... searching paths\n");
		foundManRec.assetPath = findInLibPaths(scriptName);

		if (!foundManRec.assetPath) {
			bgtrace0(2,"not found searching paths either. giving up\n");
			if (!(flags&im_quietFlag))
				assertError(NULL,"import: bash library not found in manifest nor in any system path. Default system path is '/usr/lib'. scriptName='%s'", scriptName);

			xfree(lookupName);
			ShellVar_set(vL1,"_importSetErrorCode");
			ShellVar_set(vL2,"_importSetErrorCode");
			bgtracePop();
			bgtrace1(2,"END importBashLibrary(%s)\n",scriptName);
			return 202;
		}
	}


	// if the caller passed in a var to receive the path, fill it in
	if (retVar) {
		*retVar = savestring(foundManRec.assetPath);

		// if we are only asked to get the path, return
		if (flags&im_getPathFlag) {
			ManifestRecord_cleanup(&foundManRec);
			xfree(lookupName);
			bgtracePop(); bgtrace1(2,"END importBashLibrary(%s) (getPath only)\n",scriptName);
			return 1;
		}
	}

	// TODO: SECURITY:  the bash import function checks if [ "$bgSourceOnlyUnchangable" ] && [ -w "$foundManRec.assetPath" ]

	// record that this scriptName has been sourced (do it first to avoid recursive loop)
	ShellVar_assocSet(vImportedLibraries, lookupName, foundManRec.assetPath);

	// now call the 'source' builtin to do the work
	WORD_LIST* args = NULL;
	args = make_word_list(make_word(foundManRec.assetPath), args);

	int errexitValue=0;
	if (flags & im_stopOnErrorFlag)
		errexitValue = change_flag('e', FLAG_ON);


	int ret = source_builtin(args);

	if (flags & im_stopOnErrorFlag)
		errexitValue = change_flag('e', (errexitValue)?FLAG_ON:FLAG_OFF);

	bgtrace1(3,"source_builtin returned '%d'\n",ret);

	// do FlushPendingClassConstructions
	SHELL_VAR* vClassClass = ShellVar_find("Class");
	if (vClassClass) {
		BGString pendingClassCtors;
		BGString_initFromStr(&pendingClassCtors, ShellVar_assocGet(ShellVar_find("Class"), "pendingClassCtors"));
		BGString_replaceWhitespaceWithNulls(&pendingClassCtors);
		char* pendingClass;
		while ( (pendingClass = BGString_nextWord(&pendingClassCtors)) && *pendingClass ) {
			DeclareClassEnd(pendingClass);
		}
		BGString_free(&pendingClassCtors);
	}

	dispose_words(args);
	ManifestRecord_cleanup(&foundManRec);
	xfree(lookupName);
	bgtracePop(); bgtrace1(2,"END importBashLibrary(%s)\n",scriptName);
	return ret;
}


char* relPaths[] = {
	"",
	"lib",
	"creqs",
	"core",
	"coreOnDemand",
	"plugins",
	NULL
};

char* findInLibPaths(char* scriptName)
{
	BGString searchPaths;
	BGString_init(&searchPaths, 500);
	BGString_append(&searchPaths, ":", ShellVar_get(ShellVar_find("scriptFolder")));

	char* bgSourceOnlyUnchangable = ShellVar_get(ShellVar_find("bgSourceOnlyUnchangable"));
	if (!bgSourceOnlyUnchangable || strcmp(bgSourceOnlyUnchangable,"")==0)
		BGString_append(&searchPaths, ":", ShellVar_get(ShellVar_find("bgLibPath")));

	BGString_append(&searchPaths, ":", "/usr/lib");
	BGString_replaceChar(&searchPaths,':','\0');

	BGString tryPath;
	BGString_init(&tryPath,500);
	char* onePath;
	char* foundScriptPath = NULL;
	while (!foundScriptPath && (onePath = BGString_nextWord(&searchPaths)) ) {
		for (int i=0; !foundScriptPath && relPaths[i];  i++) {
			BGString_copy(&tryPath, onePath);
			BGString_append(&tryPath, "/", relPaths[i]);
			BGString_append(&tryPath, "/", scriptName);
			if (fsExists(tryPath.buf)) {
				foundScriptPath = savestring(tryPath.buf);
			}
		}
	}
	BGString_free(&tryPath);
	BGString_free(&searchPaths);
	return foundScriptPath;
}
