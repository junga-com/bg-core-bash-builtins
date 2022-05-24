
#if !defined (_bg_import_H_)
#define _bg_import_H_

#include "bg_manifest.h"

#define im_forceFlag       0x01
#define im_stopOnErrorFlag 0x02
#define im_quietFlag       0x04
#define im_getPathFlag     0x08
#define im_devOnlyFlag     0x10

extern int importManifestCriteria(ManifestRecord* rec, ManifestRecord* target);
extern int importBashLibrary(char* scriptName, int flags, char** retVar);

#endif // _bg_import_H_
