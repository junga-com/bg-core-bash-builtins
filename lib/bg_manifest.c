
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
	dst->line      = savestring(src->line);
	dst->pkgName   = savestring(src->pkgName);
	dst->assetType = savestring(src->assetType);
	dst->assetName = savestring(src->assetName);
	dst->assetPath = savestring(src->assetPath);
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
	while (freadline(manFileFD, buf, &bufSize) > -1) {
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

		// we get here only when all the specified filters matched the record so break the loop to return the result
		// save the first match to return.
		// TODO: change this to return a dynamic array of ManifestRecord's whcih the caller must free.
		if (!retVal.assetType)
			ManifestRecord_save(&retVal, &rec);

		// TODO: change this to respect the standard output options instead of only printing to stdout
		if (outputStr) {
			char* outLine = manifestExpandOutStr(&rec, outputStr);
			printf("%s\n",outLine);
		}
	}

	xfree(buf);
	BGString_free(&parser);
	return retVal;
}
