
#include "bg_manifest.h"

#include <stdio.h>

#include "bg_bashAPI.h"
#include "BGString.h"



ManifestRecord* ManifestRecord_assign(ManifestRecord* ret, char* pkgName, char* assetType, char* assetName, char* assetPath)
{
	ret->line      = NULL;
	ret->pkgName   = pkgName;
	ret->assetType = assetType;
	ret->assetName = assetName;
	ret->assetPath = assetPath;
	ret->alloced = 0;
	return ret;
}
ManifestRecord* ManifestRecord_new(char* pkgName, char* assetType, char* assetName, char* assetPath)
{
	ManifestRecord* ret = xmalloc(sizeof(ManifestRecord));
	return ManifestRecord_assign(ret, pkgName, assetType, assetName, assetPath);
}
ManifestRecord* ManifestRecord_newFromLine(char* line)
{
	ManifestRecord* ret = xmalloc(sizeof(ManifestRecord));
	ret->line = savestring(line);
	BGString parser; BGString_initFromAllocatedStr(&parser, line);
	BGString_replaceWhitespaceWithNulls(&parser);
	ret->pkgName =   savestring(BGString_nextWord(&parser));
	ret->assetType = savestring(BGString_nextWord(&parser));
	ret->assetName = savestring(BGString_nextWord(&parser));
	ret->assetPath = savestring(BGString_nextWord(&parser));
	ret->alloced = 1;
	// we do not free parser b/c the caller owns char* line and we never call append so we did not change the allocation.
	return ret;
}
ManifestRecord* ManifestRecord_save(ManifestRecord* dst, ManifestRecord* src)
{
	dst->line      = savestring((src->line)?src->line:"");
	dst->pkgName   = savestring((src->pkgName)?src->pkgName:"");
	dst->assetType = savestring((src->assetType)?src->assetType:"");
	dst->assetName = savestring((src->assetName)?src->assetName:"");
	dst->assetPath = savestring((src->assetPath)?src->assetPath:"");
	dst->alloced = 1;
	return dst;
}

void ManifestRecord_free(ManifestRecord* rec)
{
	if (rec->alloced) {
		xfree(rec->line);      rec->line      = NULL;
		xfree(rec->pkgName);   rec->pkgName   = NULL;
		xfree(rec->assetType); rec->assetType = NULL;
		xfree(rec->assetName); rec->assetName = NULL;
		xfree(rec->assetPath); rec->assetPath = NULL;
		rec->alloced = 0;
	}
}

char* manifestExpandOutStr(ManifestRecord* rec, char* outputStr)
{
	BGString retVal;
	BGString_init(&retVal, 500);
	char* p = outputStr;
	while (p && *p) {
		char* p2 = p;
		while (*p2 && *p2 != '$')
			p2++;
		if (*p2 == '$') {
			BGString_appendn(&retVal,p, (p2-p),"");
			p = p2+1;
			switch (*p) {
				case '0': BGString_append(&retVal,rec->line     , ""); break;
				case '1': BGString_append(&retVal,rec->pkgName  , ""); break;
				case '2': BGString_append(&retVal,rec->assetType, ""); break;
				case '3': BGString_append(&retVal,rec->assetName, ""); break;
				case '4': BGString_append(&retVal,rec->assetPath, ""); break;
				default:
					BGString_appendn(&retVal,p2,2,"");
					break;
			}
			p++;
		} else {
			BGString_append(&retVal,p,"");
			p = p2;
		}
	}
	return retVal.buf;
}

// Pass in a record <target> containing match criteria and it returns the first record in the hostmanifest file that matches the
// criteria or one with all null fields if no record matches.
ManifestRecord manifestGet(char* manFile, char* outputStr, ManifestRecord* target, ManifestFilterFn filterFn)
{
//fprintf(stderr, "\n!!! starting manifestGet(%s, %s, (\n\tline:'%s'\n\tpkg:'%s'\n\ttype:'%s'\n\tname:'%s'\n\tpath:'%s'\n))\n", manFile, outputStr, target->line,target->pkgName,target->assetType,target->assetName,target->assetPath);
	ManifestRecord retVal; ManifestRecord_assign(&retVal, NULL,NULL,NULL,NULL);

	// if the caller did not specify any filters, we interpret it as there can not be any match.
	if (!target->pkgName && !target->assetType && !target->assetName && !target->assetPath && !filterFn)
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
	size_t bufSize = 500;
	char* buf =  xmalloc(bufSize);
	ManifestRecord rec;    ManifestRecord_assign(&rec,    NULL,NULL,NULL,NULL);
	BGString parser; BGString_init(&parser, 500);
	while (freadline(manFileFD, &buf, &bufSize) > -1) {
		rec.line = buf;

		// make a copy of buf in parser which modifies and consumes it.
		BGString_copy(&parser, buf);
		BGString_replaceWhitespaceWithNulls(&parser);
		rec.pkgName = BGString_nextWord(&parser);
		if (!filterFn && !matchFilter(target->pkgName ,rec.pkgName))
			continue;

		rec.assetType = BGString_nextWord(&parser);
		if (!filterFn && !matchFilter(target->assetType ,rec.assetType))
			continue;

		rec.assetName = BGString_nextWord(&parser);
		if (!filterFn && !matchFilter(target->assetName ,rec.assetName))
			continue;

		rec.assetPath = BGString_nextWord(&parser);
		if (!filterFn && !matchFilter(target->assetPath ,rec.assetPath))
			continue;

		if (filterFn && (!(*filterFn)(&rec, target)) ) {
			continue;
		}

		// we get here only when rec is a match for all the specified filters

		// only the first match gets returned in retVal
		if (!retVal.assetType)
			ManifestRecord_save(&retVal, &rec);

		// if we are printing results to stdout or returning in an array, we dont stop after the first match
		// TODO: change this to respect the standard output options instead of only printing to stdout
		if (outputStr) {
			char* outLine = manifestExpandOutStr(&rec, outputStr);
			printf("%s\n",outLine);
		} else
			break;
	}

	xfree(buf);
	BGString_free(&parser);
	return retVal;
}



// returns all records in the hostmanifest file that match the criteria.
// the return is an array of ManifestRecord where the last one is all nulls
// Example:
//    ManifestRecord target; ManifestRecord_assign(&tartget, NULL,"template(.folder)?", templateNameOrPath,NULL);
//    ManifestRecord* restults = manifestGetM(NULL, NULL, &target, NULL);
//    ManifestRecord_free(&tartget);
//    for (int i=0; results && results[i].assetType; i++) {
//        ...
//    }
//    xfree(results);
ManifestRecord* manifestGetC(char* manFile, char* outputStr, ManifestRecord* target, ManifestFilterFn filterFn)
{
	int retValCount = 0;
	int retValAllocCount = 5;
	ManifestRecord* retVal = xmalloc(retValAllocCount*sizeof(ManifestRecord));
	// set the first record to null because we may return the empty set early without hitting the code at the end
	ManifestRecord_assign(&(retVal[retValCount]),    NULL,NULL,NULL,NULL);

	// if the caller did not specify any filters, we interpret it as there can not be any match.
	if (!target->pkgName && !target->assetType && !target->assetName && !target->assetPath && !filterFn)
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
	size_t bufSize = 500;
	char* buf =  xmalloc(bufSize);
	ManifestRecord rec;    ManifestRecord_assign(&rec,    NULL,NULL,NULL,NULL);
	BGString parser; BGString_init(&parser, 500);
	while (freadline(manFileFD, &buf, &bufSize) > -1) {
		rec.line = buf;

		// make a copy of buf in parser which modifies and consumes it.
		BGString_copy(&parser, buf);
		BGString_replaceWhitespaceWithNulls(&parser);
		rec.pkgName = BGString_nextWord(&parser);
		if (!filterFn && !matchFilter(target->pkgName ,rec.pkgName))
			continue;

		rec.assetType = BGString_nextWord(&parser);
		if (!filterFn && !matchFilter(target->assetType ,rec.assetType))
			continue;

		rec.assetName = BGString_nextWord(&parser);
		if (!filterFn && !matchFilter(target->assetName ,rec.assetName))
			continue;

		rec.assetPath = BGString_nextWord(&parser);
		if (!filterFn && !matchFilter(target->assetPath ,rec.assetPath))
			continue;

		if (filterFn && (!(*filterFn)(&rec, target)) ) {
			continue;
		}

		// we get here only when rec is a match for all the specified filters

		// grow the retVal array if needed. +2 is so that we realloc enough for this rec and also the null rec at the end
		if (retValCount+2 > retValAllocCount) {
			retValAllocCount += 10;
			ManifestRecord* retVal = xrealloc(retVal, retValAllocCount*sizeof(ManifestRecord));
		}

		ManifestRecord_save(&(retVal[retValCount++]), &rec);
	}

	// we always realloc enough for a NULL record at the end so we dont need to check if there is room here
	ManifestRecord_assign(&(retVal[retValCount]),    NULL,NULL,NULL,NULL);

	xfree(buf);
	BGString_free(&parser);
	return retVal;
}
