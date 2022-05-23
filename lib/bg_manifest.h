
#if !defined (_bg_manifest_H_)
#define _bg_manifest_H_

typedef enum {mf_pkgName, mf_assetType, mf_assetName, mf_assetPath} ManifestField;
typedef struct {
    char* line;
    char* pkgName;
    char* assetType;
    char* assetName;
    char* assetPath;
    int alloced;
} ManifestRecord;

typedef int (*ManifestFilterFn)(ManifestRecord* rec, ManifestRecord* target);

extern ManifestRecord* ManifestRecord_assign(ManifestRecord* ret, char* pkgName, char* assetType, char* assetName, char* assetPath);
extern ManifestRecord* ManifestRecord_new(char* pkgName, char* assetType, char* assetName, char* assetPath);
extern ManifestRecord* ManifestRecord_newFromLine(char* line);
extern ManifestRecord* ManifestRecord_save(ManifestRecord* dst, ManifestRecord* src);
extern void ManifestRecord_free(ManifestRecord* rec);
extern char* manifestExpandOutStr(ManifestRecord* rec, char* outputStr);
extern ManifestRecord manifestGet(char* manFile, char* outputStr, ManifestRecord* target, ManifestFilterFn filterFn);

#endif // _bg_manifest_H_
