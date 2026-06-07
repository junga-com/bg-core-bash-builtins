
#if !defined (_bg_manifest_H_)
#define _bg_manifest_H_

typedef enum {mf_pkgName, mf_assetType, mf_assetName, mf_assetPath} ManifestField;
typedef struct {
	char* line;
	char* pkgName;
	char* assetType;
	char* assetName;
	char* assetPath;

	// this enables importLookup or PluginTypeLookup depending on the format. import passes
	// name being imported as scriptName
	char* scriptName;
	char* pluginName;

	char* mode; // NULL|importLookup|PluginTypeLookup
	int matchCount;
	int alloced;
} ManifestRecord;

typedef int (*ManifestFilterFn)(ManifestRecord* rec, ManifestRecord* target);

extern void ManifestRecord_assignFromInputLine(ManifestRecord* pR, char* line);
extern void ManifestRecord_cleanup(ManifestRecord* rec);
extern int ManifestRecord_match(ManifestRecord* toTest, ManifestRecord* criteria);


extern int ManifestRecord_isEmpty(ManifestRecord* pR);
extern int ManifestRecord_criteriaIsEmpty(ManifestRecord* pR);

extern void ManifestRecord_bgtrace(ManifestRecord* rec, char* label);
extern ManifestRecord* ManifestRecord_makeImportCriteria(ManifestRecord* ret, char* scriptName);
extern ManifestRecord* ManifestRecord_assign(ManifestRecord* ret, char* pkgName, char* assetType, char* assetName, char* assetPath);
extern ManifestRecord* ManifestRecord_new(char* pkgName, char* assetType, char* assetName, char* assetPath);
extern ManifestRecord* ManifestRecord_newFromLine(char* line);
extern ManifestRecord* ManifestRecord_save(ManifestRecord* dst, ManifestRecord* src);
extern void ManifestRecord_free(ManifestRecord* rec);
extern void ManifestRecord_arrayFree(ManifestRecord* pDynArray);
extern char* ManifestRecord_expandTemplate(ManifestRecord* rec, char* outputStr);

// OBSOLETE? use manifestGetC
extern ManifestRecord manifestGetOne(char* manFile, char* outputStr, ManifestRecord* target, ManifestFilterFn filterFn);

// This 'C' version returns 0 or more matching records instead of only the first. The caller must xfree() the returned array
extern ManifestRecord* manifestGetC(char* manFile, char* outputStr, ManifestRecord* target, ManifestFilterFn filterFn);

#endif // _bg_manifest_H_
