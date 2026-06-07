
#include "bg_manifest.h"

#include <stdio.h>

#include "bg_bashAPI.h"
#include "BGString.h"
#include "bg_misc.h"



void ManifestRecord_bgtrace(ManifestRecord* pR, char* label) {
	int oneLine = 1;

	if (oneLine) {
		BGString s;
		BGString_init(&s, 4096);
		BGString_append(&s,"", label);

		if (!pR->mode) {
			BGString_appendf(&s,"", " %s %s %s %d", pR->pkgName?pR->pkgName:"--", pR->assetType?pR->assetType:"--", pR->assetName?pR->assetName:"--", pR->pkgName?strlen(pR->pkgName):0);

		} else if (strcmp(pR->mode,"importLookup")==0) {
			BGString_appendf(&s,"", " IMPCRIT: %s ", pR->scriptName?pR->scriptName:"--");

		} else {
			BGString_appendf(&s,"", " PLGCRIT: %s ", pR->pluginName?pR->pluginName:"--");
		}

		__bgtrace("%s:\n", s.buf);

		BGString_free(&s);

	} else {
		__bgtrace("%s \n", label?label:"");
		if (!pR) {
			__bgtrace(" | NULL ptr\n");
			return;
		}
		if (pR->line)       __bgtrace("  | line       : '%s'\n", pR->line       ?pR->line        :"NULL");
		if (pR->pkgName)    __bgtrace("  | pkgName    : '%s'\n", pR->pkgName    ?pR->pkgName     :"NULL");
		if (pR->assetType)  __bgtrace("  | assetType  : '%s'\n", pR->assetType  ?pR->assetType   :"NULL");
		if (pR->assetName)  __bgtrace("  | assetName  : '%s'\n", pR->assetName  ?pR->assetName   :"NULL");
		if (pR->assetPath)  __bgtrace("  | assetPath  : '%s'\n", pR->assetPath  ?pR->assetPath   :"NULL");
		if (pR->scriptName) __bgtrace("  | scriptName : '%s'\n", pR->scriptName ?pR->scriptName  :"NULL");
		if (pR->pluginName) __bgtrace("  | pluginName : '%s'\n", pR->pluginName ?pR->pluginName  :"NULL");
		__bgtrace("  | mode(%s) matchCount(%d)  alloced(%d)\n", pR->mode?pR->mode:"NULL", pR->matchCount, pR->alloced);
	}
}

//##############################################################################
//# Construction, Assignment, Destruction


ManifestRecord* ManifestRecord_assign(ManifestRecord* ret, char* pkgName, char* assetType, char* assetName, char* assetPath)
{
	ret->line      = NULL;
	ret->pkgName   = pkgName;
	ret->assetType = assetType;
	ret->assetName = assetName;
	ret->assetPath = assetPath;

	ret->scriptName = NULL;
	ret->pluginName = NULL;
	ret->mode       = NULL; // NULL|importLookup|PluginTypeLookup

	ret->matchCount = 0;
	ret->alloced = 0;
	return ret;
}

// line is a wriatable null terminated buffer that the caller owns. It will be mutated to replace all
// witespace with nulls.
// copies will be made from that buffer so after calling this the record will not reference it
void ManifestRecord_assignFromInputLine(ManifestRecord* pR, char* line)
{
	pR->line = savestring(line);

	BGString parser; BGString_initFromAllocatedStr(&parser, line);
	BGString_replaceWhitespaceWithNulls(&parser);

	pR->pkgName   = safeSaveString(BGString_nextWord(&parser));
	pR->assetType = safeSaveString(BGString_nextWord(&parser));
	pR->assetName = safeSaveString(BGString_nextWord(&parser));
	pR->assetPath = safeSaveString(BGString_nextWord(&parser));

	pR->scriptName = savestring("");
	pR->pluginName = savestring("");
	pR->mode       = NULL;
	pR->matchCount = 0;

	pR->alloced = 1;
	// we do not free parser b/c the caller owns char* line and we never call append so we did not change the allocation.
}

ManifestRecord* ManifestRecord_makeImportCriteria(ManifestRecord* ret, char* scriptName)
{
	ret->line      = NULL;
	ret->pkgName   = NULL;
	ret->assetType = NULL;
	ret->assetName = NULL;
	ret->assetPath = NULL;

	ret->scriptName = savestring(scriptName);
	ret->pluginName = NULL;
	ret->mode       = NULL; // NULL|importLookup|PluginTypeLookup

	if (scriptName && *scriptName) {
		if (strncmp(scriptName, "PluginType.", 11) == 0 || strncmp(scriptName, "PluginType:", 11) == 0) {
			ret->mode = savestring("PluginTypeLookup");
			ret->pluginName = savestring(scriptName + 11);

		} else {
			ret->mode = savestring("importLookup");
			size_t len = strlen(scriptName);

			if (len > 11 && strcmp(scriptName + len - 11, ".PluginType") == 0) {
				ret->mode = savestring("PluginTypeLookup");
				ret->pluginName = savestring(scriptName);
				ret->pluginName[len - 11] = '\0';

			} else if (len > 11 && strcmp(scriptName + len - 11, ":PluginType") == 0) {
				ret->mode = savestring("PluginTypeLookup");
				ret->pluginName = savestring(scriptName);
				ret->pluginName[len - 11] = '\0';
			}
		}
	}

	ret->matchCount = 0;
	ret->alloced = 1;
	return ret;
}

ManifestRecord* ManifestRecord_new(char* pkgName, char* assetType, char* assetName, char* assetPath)
{
	ManifestRecord* ret = xmalloc(sizeof(ManifestRecord));
	return ManifestRecord_assign(ret, pkgName, assetType, assetName, assetPath);
}

// overwrite dst fields with new copies of src fields
ManifestRecord* ManifestRecord_save(ManifestRecord* dst, ManifestRecord* src)
{
	dst->line      = savestring((src->line)?src->line:"");
	dst->pkgName   = savestring((src->pkgName)?src->pkgName:"");
	dst->assetType = savestring((src->assetType)?src->assetType:"");
	dst->assetName = savestring((src->assetName)?src->assetName:"");
	dst->assetPath = savestring((src->assetPath)?src->assetPath:"");

	dst->scriptName = savestring("");
	dst->pluginName = savestring("");
	dst->mode       = NULL;
	dst->matchCount = 0;

	dst->alloced = 1;
	return dst;
}

void ManifestRecord_cleanup(ManifestRecord* rec)
{
	if (rec->alloced) {
		if (rec->line)      {xfree(rec->line);      rec->line      = NULL;}
		if (rec->pkgName)   {xfree(rec->pkgName);   rec->pkgName   = NULL;}
		if (rec->assetType) {xfree(rec->assetType); rec->assetType = NULL;}
		if (rec->assetName) {xfree(rec->assetName); rec->assetName = NULL;}
		if (rec->assetPath) {xfree(rec->assetPath); rec->assetPath = NULL;}

		if (rec->scriptName) {xfree(rec->scriptName); rec->scriptName = NULL;}
		if (rec->pluginName) {xfree(rec->pluginName); rec->pluginName = NULL;}

		rec->alloced = 0;
	}
}

void ManifestRecord_free(ManifestRecord* pDynRec) {
	if (!pDynRec)
		return;
	ManifestRecord_cleanup(pDynRec);
	xfree(pDynRec);
}

void ManifestRecord_arrayFree(ManifestRecord* pDynArray) {
	if (!pDynArray)
		return;
	for (ManifestRecord* pR = pDynArray; !ManifestRecord_isEmpty(pR); pR++ )
		ManifestRecord_cleanup(pR);
	xfree(pDynArray);
}



//##############################################################################
//# Things you can do with ManifestRecords


int ManifestRecord_isEmpty(ManifestRecord* pR) {
	if (!pR)           return 1;
	if (pR->pkgName)   return 0;
	if (pR->assetType) return 0;
	if (pR->assetName) return 0;
	if (pR->assetPath) return 0;
	return 1;
}

int ManifestRecord_criteriaIsEmpty(ManifestRecord* pR) {
	if (!pR)           return 1;
	if (pR->pkgName)   return 0;
	if (pR->assetType) return 0;
	if (pR->assetName) return 0;
	if (pR->assetPath) return 0;

	if (pR->scriptName) return 0;
	if (pR->pluginName) return 0;
	if (pR->mode)       return 0;
	return 1;
}


// expand a template string against the values of the fields in rec
// $0  : the whole manifest line
// $1  : the pkgName
// $2  : the assetType
// $3  : the assetname
// $4  : the filename
// Params:
//    outputStr : default is "$0". template string.
// Return Value:
// You own the returned string. xfree(retVal) when done
char* ManifestRecord_expandTemplate(ManifestRecord* rec, char* outputStr)
{
	BGString retVal;
	BGString_init(&retVal, 500);

	char* p = outputStr ? outputStr : "$0";

	while (*p) {
		char* p2 = p;

		while (*p2 && *p2 != '$')
			p2++;

		if (p2 > p)
			BGString_appendn(&retVal, "", p, p2 - p);

		if (!*p2)
			break;

		// p2 points at '$'
		p = p2 + 1;

		switch (*p) {
			case '0': BGString_append(&retVal, "", rec->line      ? rec->line      : ""); p++; break;
			case '1': BGString_append(&retVal, "", rec->pkgName   ? rec->pkgName   : ""); p++; break;
			case '2': BGString_append(&retVal, "", rec->assetType ? rec->assetType : ""); p++; break;
			case '3': BGString_append(&retVal, "", rec->assetName ? rec->assetName : ""); p++; break;
			case '4': BGString_append(&retVal, "", rec->assetPath ? rec->assetPath : ""); p++; break;

			// '$' at end, or '$x' where x is not 0-4: keep the literal '$'
			default:
				BGString_appendn(&retVal, "", "$", 1);
				break;
		}
	}

	return retVal.buf;
}

// Params:
//    toTest : is a record from one line in the manifest awkdata file. the fields
//             pkgName,assetType,assetName,assetPath
//             are qaranteed to be non NULL.
//    criterial: is a record that holds matching criteria. When a field is NULL there
//             is no match criteria for that field
//             criterial->mode :
//                 NULL  : one or more pkgName,assetType,assetName,assetPath fields
//                         should be non-NULL and contains a regex
//                 importLookup : criterial->scriptName is matched against assetName or assetPath
//                 pluginLookup : criterial->pluginTypeName is matched against assetName or assetPath
// Helper Functions:
// matchFilter(fieldMatchRegExStr, toTestFieldString)
//    This helper function is available.
//    if fieldMatchRegExStr is NULL or empty it returns true(1)
//    fieldMatchRegExStr is used in  regcomp(&regex, fieldMatchRegExStr, REG_EXTENDED) an dapplied against toTestFieldString
// Return Value:
//     true(1) if toTest is a match agains criteria
//     false(0) if it is not a match
int ManifestRecord_match(ManifestRecord* toTest, ManifestRecord* criteria) {
	if (!toTest || !criteria)
		return 0;

	// normal field-regex criteria mode
	if (!criteria->mode) {
		return
			matchFilter(criteria->pkgName,   toTest->pkgName)   &&
			matchFilter(criteria->assetType, toTest->assetType) &&
			matchFilter(criteria->assetName, toTest->assetName) &&
			matchFilter(criteria->assetPath, toTest->assetPath);
	}

	// awk:
	// mode=="importLookup" && $2=="lib.script.bash" && ($3==baseName || $4 ~ "(^|/)" scriptName "$")
	// mode=="importLookup" && $2=="plugin"          && ($3==baseName || $4 ~ "(^|/)" scriptName "$")
	// mode=="importLookup" && $2=="unitTest"        && ($3==baseName || $4 ~ "(^|/)" scriptName "$")
	if (strcmp(criteria->mode, "importLookup") == 0) {
		if (
			strcmp(toTest->assetType, "lib.script.bash") != 0 &&
			strcmp(toTest->assetType, "plugin")          != 0 &&
			strcmp(toTest->assetType, "unitTest")        != 0
		)
			return 0;

		if (criteria->assetName && strcmp(toTest->assetName, criteria->assetName) == 0)
			return 1;

		if (criteria->scriptName) {
			char* pathRegex = saprintf("(^|/)%s$", criteria->scriptName);
			int matched = matchFilter(pathRegex, toTest->assetPath);
			xfree(pathRegex);
			return matched;
		}

		return 0;
	}

	// awk:
	// mode=="PluginTypeLookup" && $2=="PluginType" && (
	//   $3 == "PluginType:" pluginTypeName ||
	//   $3 == "PluginType." pluginTypeName ||
	//   $3 == pluginTypeName ":PluginType" ||
	//   $3 == pluginTypeName ".PluginType" ||
	//   $4 ~ "(^|/)" pluginTypeName "[.]PluginType$"
	// )
	if (strcmp(criteria->mode, "PluginTypeLookup") == 0) {
		if (strcmp(toTest->assetType, "PluginType") != 0)
			return 0;

		if (!criteria->pluginName || !*criteria->pluginName)
			return 0;

		char* n1 = saprintf("PluginType:%s", criteria->pluginName);
		char* n2 = saprintf("PluginType.%s", criteria->pluginName);
		char* n3 = saprintf("%s:PluginType", criteria->pluginName);
		char* n4 = saprintf("%s.PluginType", criteria->pluginName);

		int matched =
			strcmp(toTest->assetName, n1) == 0 ||
			strcmp(toTest->assetName, n2) == 0 ||
			strcmp(toTest->assetName, n3) == 0 ||
			strcmp(toTest->assetName, n4) == 0;

		xfree(n1);
		xfree(n2);
		xfree(n3);
		xfree(n4);

		if (matched)
			return 1;

		char* pathRegex = saprintf("(^|/)%s[.]PluginType$", criteria->pluginName);
		matched = matchFilter(pathRegex, toTest->assetPath);
		xfree(pathRegex);

		return matched;
	}

	return 0;
}


//##############################################################################
//# manifestGet* entry point functions


// Pass in a record <target> containing match criteria and it returns the first record in the hostmanifest file that matches the
// criteria or one with all null fields if no record matches.
// Return Value:
// you own the ManifestRecord dynamic strings. Call ManifestRecord_free($retVal) when done with it
ManifestRecord manifestGetOne(char* manFile, char* outputStr, ManifestRecord* pTarget, ManifestFilterFn filterFn)
{
	if (pTarget)
		pTarget->matchCount = 1;
	ManifestRecord* pResults = manifestGetC(manFile, outputStr, pTarget, filterFn);

	ManifestRecord retVal;
	ManifestRecord_save(&retVal, &pResults[0]);

	ManifestRecord_arrayFree(pResults);
	return retVal;
}

// returns all records in the hostmanifest file that match the criteria.
// the return is an array of ManifestRecord where the last one is all nulls
// Example:
//    ManifestRecord target; ManifestRecord_assign(&tartget, NULL,"template(.folder)?", templateNameOrPath,NULL);
//    ManifestRecord* restults = manifestGetC(NULL, NULL, &target, NULL);
//    ManifestRecord_free(&tartget);
//    for (int i=0; results && results[i].assetType; i++) {
//        ...
//    }
//    ManifestRecord_arrayFree(results);
ManifestRecord* manifestGetC(char* manFile, char* outputStr, ManifestRecord* pTarget, ManifestFilterFn filterFn)
{
	int retValCount = 0;
	int retValAllocCount = 5;

	// set the first record to null because we may return the empty set early without hitting the code at the end
	ManifestRecord* retVal = xmalloc(retValAllocCount*sizeof(ManifestRecord));
	ManifestRecord_assign(&(retVal[retValCount]),    NULL,NULL,NULL,NULL);

	// if the caller did not specify any filters, we interpret it as there can not be any match.
	if (ManifestRecord_criteriaIsEmpty(pTarget))
		return retVal;

	if (!manFile)
		manFile = ShellVar_get(ShellVar_find("bgVinstalledManifest"));
	if (!manFile || strcmp(manFile,"")==0)
		manFile = "/var/lib/bg-core/manifest";
	FILE* manFileFD = fopen(manFile, "r");
	if (!manFileFD)
		return retVal;

	// bg-core\0       awkDataSchema\0   plugins\0         /usr/lib/plugins.awkDataSchema\0
	// '--<pkgName>'   '--<assetType>'   '--<assetName>'   '--<assetPath >'
	size_t bufSize = 4099;
	char* buf =  xmalloc(bufSize);
	ManifestRecord* pRec;
	int matched = 0;
	while (freadline(manFileFD, &buf, &bufSize) > -1) {
		pRec = &(retVal[retValCount]);
		ManifestRecord_assignFromInputLine(pRec, buf);

		matched = 0;
		if (filterFn)
			matched = ( (*filterFn)(pRec, pTarget) ) ;

		if (!matched)
			matched = ManifestRecord_match(pRec, pTarget);

		if (matched) {
			// the current line is already in the last record so we just have to increment to make it permanent
			// so it wont be overwritten by the next line
			retValCount++;

			// make sure we have enough room for the null record
			if (retValCount+1 > retValAllocCount) {
				retValAllocCount += 10;
				retVal = xrealloc(retVal, retValAllocCount*sizeof(ManifestRecord));
			}

			if (pTarget->matchCount==1)
				break;
		}
	}

	// we always realloc enough for a NULL record at the end so we dont need to check if there is room here
	// use ManifestRecord_isEmpty to test for the end
	ManifestRecord_assign(&(retVal[retValCount]),    NULL,NULL,NULL,NULL);

	fclose(manFileFD);
	xfree(buf);
	return retVal;
}
