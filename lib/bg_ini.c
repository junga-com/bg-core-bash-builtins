
#include "bg_ini.h"

#include <errno.h>
#include <regex.h>
#include <sys/stat.h>

#include "bg_bashAPI.h"
#include "bg_templates.h"
#include "BGString.h"

#define min(a,b) (((a)<(b))?(a):(b))
#define max(a,b) (((a)>(b))?(a):(b))

typedef enum {
	ilt_invalid=0,
	ilt_comment,
	ilt_whitespace,
	ilt_section,
	ilt_setting
} IniLineType;

char* IniLineType_toString(IniLineType t)
{
	switch (t) {
		case ilt_invalid:    return "invalid";
		case ilt_comment:    return "comment";
		case ilt_whitespace: return "whitespace";
		case ilt_section:    return "section";
		case ilt_setting:    return "setting";
	}
	return "unknown linetype";
}

// line/lineAllocSize is a dynamically allocated buffer. The other char* members are references to inside that buffer and have
// corresponding Length fields to determine where they end.
typedef struct {
	int         NR;  // line number in file
	char*       line;
	size_t      lineAllocSize;
	IniLineType lineType;
	char*       section;    int sectionLen;
	char*       paramName;  int paramNameLen;
	char*       paramValue; int paramValueLen;
	char*       paramQuoteMode;
	char*       comment;
} IniLine;

typedef struct {
	int linesAllocSize;
	int count;
	int isLastLineBlank;
	IniLine lines[];
} IniLineBuffer;



extern char* parserFindEndOfValue(char* pS);
extern void IniLine_parse(IniLine* pLine, IniScheme* pScheme);


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// IniLine

#define LineType_isData(lt) (lt==ilt_section || lt==ilt_setting)


void IniLine_free(IniLine* pLine)
{
	if (pLine->line)
		xfree(pLine->line);
	pLine->line = NULL;
	pLine->lineAllocSize = 0;
}

void IniLine_allocSpaceFor(IniLine* pLine, int newSize)
{
	int new = (!pLine->line);
	if (newSize > pLine->lineAllocSize) {
		pLine->lineAllocSize = newSize;
		pLine->line = xrealloc(pLine->line, pLine->lineAllocSize);
	}
	if (new)
		*pLine->line = '\0';
}

void IniLine_dump(IniLine* pBuf) {
	__bgtrace("   IniLine: '%s'\n", pBuf->line);
	__bgtrace("      NR            : '%d'\n", pBuf->NR);
	__bgtrace("      lineAllocSize : '%d'\n", pBuf->lineAllocSize);
	__bgtrace("      lineType      : '%s'\n", IniLineType_toString(pBuf->lineType));
	__bgtrace("      section       : '%s'\n", savestringn(pBuf->section, pBuf->sectionLen));
	__bgtrace("      paramName     : '%s'\n", savestringn(pBuf->paramName, pBuf->paramNameLen));
	__bgtrace("      paramValue    : '%s'\n", savestringn(pBuf->paramValue, pBuf->paramValueLen));
	__bgtrace("      paramQuoteMode:  %s \n", pBuf->paramQuoteMode);
	__bgtrace("      comment       : '%s'\n", pBuf->comment);
}

#define IniLine_isSectionEqTo(  pLine, _section)     ( (pLine)->sectionLen==strlen(_section)       && strncmp(_section,    (pLine)->section,    (pLine)->sectionLen)==0 )
#define IniLine_isParamNameEqTo(pLine, _paramName)   ( (pLine)->paramNameLen==strlen(_paramName)   && strncmp(_paramName,  (pLine)->paramName,  (pLine)->paramNameLen)==0 )
#define IniLine_isParamValueEqTo(pLine, _paramValue) ( (pLine)->paramValueLen==strlen(_paramValue) && strncmp(_paramValue, (pLine)->paramValue, (pLine)->paramValueLen)==0 )


// after pLine->line is changed, this will parse its contents to fill in the other pLine fields
void IniLine_parse(IniLine* pLine, IniScheme* pScheme)
{
	if (!pLine)
		return;

	pLine->section    = NULL;  pLine->sectionLen    = 0;
	pLine->paramName  = NULL;  pLine->paramNameLen  = 0;
	pLine->paramValue = NULL;  pLine->paramValueLen = 0;
	pLine->comment    = NULL;

	// nameValueDelim is the first char in the pScheme->delim with default being '='
	char nameValueDelim = (pScheme->delim) ? *pScheme->delim : '=';

	char* pS = pLine->line;
	char* pE = pLine->line;
	while (*pS && whitespace(*pS)) pS++;

	if        (*pS == '\0') {
		pLine->lineType = ilt_whitespace;
	} else if (*pS == '#') {
		pLine->lineType = ilt_comment;
	} else if (*pS == '[') {
		pLine->lineType = ilt_section;
		pS++; while (*pS && whitespace(*pS)) pS++;
		pE = pS; while (*pE && ! whitespace(*pE) && *pE != ']') pE++;

		pLine->section = pS;
		pLine->sectionLen = (pE-pS);

	} else {
		pLine->lineType = ilt_setting;

		pE = pS; while (*pE && ! whitespace(*pE) && *pE != nameValueDelim) pE++;
		if (*pE=='\0') {
			pLine->lineType = ilt_invalid;
			return; // its invalid but we can still process more lines so return true
		}

		// pS to pE is now the setting name.
		pLine->paramName = pS;
		pLine->paramNameLen = (pE-pS);

		// there could be whitespace before the nameValueDelim so advance if needed.
		pS = pE; while (*pS && whitespace(*pS) && *pS != nameValueDelim) pS++;
		if (*pS != nameValueDelim) {
			pLine->lineType = ilt_invalid;
			return; // its invalid but we can still process more lines so return true
		}

		// advance over whitespace after the nameValueDelim if needed (quotes can be used if leading space is part of the value)
		pS++; while (*pS && whitespace(*pS)) pS++;

		// the value may be quoted and there may be an EOL comment so use a helper function to find the end.
		pE = parserFindEndOfValue(pS);

		// remove quotes if present
		pLine->paramQuoteMode="";
		if ( (*pS=='"' && (pE>pS) && *(pE-1)=='"') ) {
			pLine->paramQuoteMode="\"";
			pS++;
			pE--;
		}
		if ( (*pS=='\'' && (pE>pS) && *(pE-1)=='\'') ) {
			pLine->paramQuoteMode="'";
			pS++;
			pE--;
		}

		// ok, we got it
		pLine->paramValue = pS;
		pLine->paramValueLen = (pE-pS);

		// if parserFindEndOfValue did its job right, this block will set pLine->comment to either the '#' of an EOL comment or the
		// terminating '\0'
		if (*pLine->paramQuoteMode) pE++;
		while (*pE && whitespace(*pE)) pE++;
		pLine->comment = pE;
	}
}


// returns true if it read a line and false if it could not (EOF typically)
// it uses pLine->line as a buffer to store the line and will allocate/growth it as needed
int IniLine_read(IniLine* pLine, IniScheme* pScheme, FILE* iniFileFD)
{
	if (!pLine)
		return 0;

	pLine->lineType = ilt_invalid;

	pLine->section    = NULL;  pLine->sectionLen    = 0;
	pLine->paramName  = NULL;  pLine->paramNameLen  = 0;
	pLine->paramValue = NULL;  pLine->paramValueLen = 0;
	pLine->comment    = NULL;

	int readLen = freadline(iniFileFD, &(pLine->line), &(pLine->lineAllocSize));

	if (readLen < 0)
		return 0;

	IniLine_parse(pLine, pScheme);

	return 1;
}

void IniLine_setToWhitespace(IniLine* pLine)
{
	if (!pLine)
		return;

	pLine->lineType = ilt_whitespace;

	IniLine_allocSpaceFor(pLine, 1);
	*pLine->line = '\0';

	pLine->section    = NULL;  pLine->sectionLen    = 0;
	pLine->paramName  = NULL;  pLine->paramNameLen  = 0;
	pLine->paramValue = NULL;  pLine->paramValueLen = 0;
	pLine->comment    = NULL;
}

char* _getCommentSeparator(char* comment)
{
	char* commentSeparator = "";
	if (comment) {
		int hasLeadingWhitespace = *comment!=' ' && *comment!='\t';
		int hasPound = 0; for (char* s=comment; !hasPound && s && *s && (*s==' '||*s=='\t'||*s=='#'); s++) if (*s=='#') hasPound=1;
		commentSeparator = (comment && !hasPound) ? " # "  : (comment && !hasLeadingWhitespace) ? " " : "" ;
	}
	return commentSeparator;
}

void IniLine_setToComment(IniLine* pLine, IniScheme* pScheme, char* comment)
{
	if (!pLine || (pScheme && strcmp("NONE", pScheme->commentsStyle)==0))
		return;

	pLine->lineType = ilt_comment;

	char* pS = comment;
	while (pS && *pS && *pS!='#') pS++;

	char* commentStart = "";
	if (pS && *pS!='#' ) {
		if (comment && whitespace(*comment))
			commentStart = "#";
		else
			commentStart = "# ";
	}

	IniLine_allocSpaceFor(pLine, bgstrlen(comment) + bgstrlen(commentStart) +1);
	sprintf(pLine->line, "%s%s", commentStart, comment);
	pLine->comment = pLine->line;
}

void IniLine_setToSetting(IniLine* pLine, IniScheme* pScheme, char* name, char* value)
{
	if (!pLine)
		return;

	pLine->lineType = ilt_setting;

	// nameValueDelim is the first char in the pScheme->delim with default being '='
	char nameValueDelim = (pScheme->delim) ? *pScheme->delim : '=';

	pLine->paramQuoteMode = pScheme->quoteMode;

	if (*pLine->paramQuoteMode=='\0' && strchr(value,'#'))
		pLine->paramQuoteMode = "\"";

	// <name><paramPad><delim><paramPad><quoteMode><value><quoteMode><commentSeparator><comment>
	//   foo              =                           5                     #        some note
	IniLine_allocSpaceFor(pLine, 3 + bgstrlen(name) + bgstrlen(value) + 3 + bgstrlen(pScheme->comment) + 2*bgstrlen(pScheme->paramPad) + 2*bgstrlen(pLine->paramQuoteMode) + 1);

	char* pS = pLine->line;
	strcpy(pS, name);                   while (*pS) pS++;
	strcpy(pS, pScheme->paramPad);      while (*pS) pS++;
	*pS++ = nameValueDelim;             *pS = '\0';
	strcpy(pS, pScheme->paramPad);      while (*pS) pS++;
	strcpy(pS, pLine->paramQuoteMode);  while (*pS) pS++;
	strcpy(pS, value);                  while (*pS) pS++;
	strcpy(pS, pLine->paramQuoteMode);  while (*pS) pS++;
	if (pScheme->comment) {
		char* commentSeparator = _getCommentSeparator(pScheme->comment);
		strcpy(pS, commentSeparator);   while (*pS) pS++;
		strcpy(pS, pScheme->comment);   while (*pS) pS++;
	}

	IniLine_parse(pLine, pScheme);
}

void IniLine_updateSettingValue(IniLine* pLine, IniScheme* pScheme, char* name, char* value)
{
	if (!pLine)
		return;

	if (pLine->lineType != ilt_setting || strncmp(name, pLine->paramName, pLine->paramNameLen)!=0)
		assertError(NULL, "logic error. IniLine_updateSettingValue should be called on a IniLine that is already a setting");

	if (!value)
		value = "";

	// make paramValue include its quotes if present
	if (*pLine->paramQuoteMode) {
		pLine->paramValue--;
		pLine->paramValueLen += 2;
	}

	// if the new value contains a '#' and the old value was not already using quotes, turn on quotes
	if (*pLine->paramQuoteMode=='\0' && strchr(value,'#'))
		pLine->paramQuoteMode = "\"";

	int newValueLen = strlen(value);
	int valLenChange = newValueLen + ( (*pLine->paramQuoteMode)? 2:0 ) - pLine->paramValueLen;

	// grow if needed
	if (valLenChange>0) {
		pLine->lineAllocSize += valLenChange;
		pLine->line = xrealloc(pLine->line, pLine->lineAllocSize);
	}

	// move what ever follows the value (which might just be the terminating '\0') by valLenChange to make the space for the value correct
	char* pS = pLine->paramValue + pLine->paramValueLen;
	memmove(pS, pS+valLenChange, strlen(pS)+1);
	pLine->comment += valLenChange;

	// shrink if needed
	if (valLenChange<0) {
		pLine->lineAllocSize += valLenChange;
		pLine->line = xrealloc(pLine->line, pLine->lineAllocSize);
	}

	// copy the new value into the space which is now the correct size
	pS = pLine->paramValue;
	if (*pLine->paramQuoteMode)
		{memmove(pS, pLine->paramQuoteMode, 1);  pS++;}
	memmove(pS, value, newValueLen);             pS += newValueLen;
	if (*pLine->paramQuoteMode)
		{memmove(pS, pLine->paramQuoteMode, 1);  pS++;}

	// make paramValue exclude the quotes if present
	if (*pLine->paramQuoteMode) {
		pLine->paramValue++;
		pLine->paramValueLen -= 2;
	}
}


void IniLine_setToSection(IniLine* pLine, IniScheme* pScheme, char* name)
{
	if (!pLine)
		return;

	pLine->lineType = ilt_section;

	// [<sectPad><name><sectPad>]<commentSeparator><comment>
	IniLine_allocSpaceFor(pLine, 2 + bgstrlen(name) +  2*bgstrlen(pScheme->sectPad) + 3 + bgstrlen(pScheme->comment) + 1);

	char* pS = pLine->line;
	*pS++ = '[';                  *pS = '\0';
	strcpy(pS, pScheme->sectPad); while (*pS) pS++;
	strcpy(pS, name);             while (*pS) pS++;
	strcpy(pS, pScheme->sectPad); while (*pS) pS++;
	*pS++ = ']';                  *pS = '\0';
	if (pScheme->sectComment) {
		char* commentSeparator = _getCommentSeparator(pScheme->sectComment);
		strcpy(pS, commentSeparator); while (*pS) pS++;
		strcpy(pS, pScheme->sectComment); while (*pS) pS++;
	}

	IniLine_parse(pLine, pScheme);
}


// txtLine is one line from a file
void IniLine_setFromText(IniLine* pLine, IniScheme* pScheme, char* txtLine)
{
	if (!pLine)
		return;

	IniLine_allocSpaceFor(pLine, strlen(txtLine)+1);
	strcpy(pLine->line, txtLine);
	IniLine_parse(pLine,pScheme);
}



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// IniLineBuffer

void IniLineBuffer_free(IniLineBuffer* pBuf)
{
	for (int i=0; i<pBuf->linesAllocSize; i++)
		IniLine_free(&(pBuf->lines[i]));
	xfree(pBuf);
}

int IniLineBuffer_isLastLineWhitespace(IniLineBuffer* pBuf)
{
	if (pBuf->count > 0)
		return (pBuf->lines[pBuf->count-1].lineType == ilt_whitespace);
	else
		return pBuf->isLastLineBlank;
}

void IniLineBuffer_dump(IniLineBuffer* pBuf)
{
	if (!pBuf) {
		__bgtrace("IniLineBuffer: (null)\n");
		return;
	}
	__bgtrace("IniLineBuffer: 0-%d used. %d lines allocated\n", pBuf->count, pBuf->linesAllocSize);
	if (pBuf->count==0)
		__bgtrace("   isLastLineBlank=%d\n",pBuf->isLastLineBlank);
	for (int i=0; i<pBuf->count; i++) {
		IniLine_dump(pBuf->lines + i);
	}
}

// grow the allocation if needed to accomodate <numAdditionalLines> lines that will be added. If the current allocation is already
// large enough, there will be no change. The memory for lines is at the end of IniLineBuffer so reallocating the memory may change
// the pointer to the IniLineBuffer which is why it is returned from this function.
// Params:
//    <numAdditionalLines> : the number of lines, in addition to any lines already used if any, that the allocation should accomadate.
void IniLineBuffer_allocateForAdditionalLines(IniLineBuffer** ppBuf, int numAdditionalLines)
{
	int prevNumAlloc = 0;
	if (!(*ppBuf)) {
		if (numAdditionalLines < 20)
			numAdditionalLines = 20;
		(*ppBuf) = xmalloc(sizeof(IniLineBuffer) + numAdditionalLines*sizeof(IniLine));
		(*ppBuf)->linesAllocSize = numAdditionalLines;
		(*ppBuf)->count = 0;
		memset(&((*ppBuf)->lines[(*ppBuf)->count]), 0,  ((*ppBuf)->linesAllocSize - prevNumAlloc)*sizeof(IniLine));
	} else if (((*ppBuf)->count+numAdditionalLines) > (*ppBuf)->linesAllocSize) {
		prevNumAlloc = (*ppBuf)->linesAllocSize;
		(*ppBuf)->linesAllocSize = 2*(*ppBuf)->linesAllocSize;
		(*ppBuf) = xrealloc((*ppBuf), sizeof(IniLineBuffer) + (*ppBuf)->linesAllocSize*sizeof(IniLine));
		memset(&((*ppBuf)->lines[(*ppBuf)->count]), 0,  ((*ppBuf)->linesAllocSize - prevNumAlloc)*sizeof(IniLine));
	}
}

IniLine* IniLineBuffer_next(IniLineBuffer** ppBuf)
{
	IniLineBuffer_allocateForAdditionalLines(ppBuf, 1);
	return (*ppBuf)->lines + (*ppBuf)->count++;
}

void IniLineBuffer_flushAll(IniLineBuffer* pBuf, FILE* tmpFD)
{
	//IniLineBuffer_dump(pBuf);
	if (pBuf->count>0)
		pBuf->isLastLineBlank = (pBuf->lines[pBuf->count-1].lineType==ilt_whitespace);

	for (int i=0; i < pBuf->count; i++) {
		int lineLen = bgstrlen(pBuf->lines[i].line);
		if (lineLen>0) {
			pBuf->lines[i].line[lineLen] = '\n';
			fwrite(pBuf->lines[i].line, 1, lineLen+1, tmpFD);
			pBuf->lines[i].line[lineLen] = '\0';
		} else
			fwrite("\n", 1,1, tmpFD);
	}

	pBuf->count = 0;
}


void IniLineBuffer_flushSection(IniLineBuffer* pBuf, FILE* tmpFD)
{
	//IniLineBuffer_dump(pBuf);
	// O0123456789
	// O-----^
	int iEnd = pBuf->count-1;
	if (iEnd>=0 && pBuf->lines[iEnd].lineType==ilt_section) iEnd--;
	while (iEnd>=0 && pBuf->lines[iEnd].lineType == ilt_comment) iEnd--;

	// now iEnd points to the first line that is not part of the current section.
	iEnd++;

	if (iEnd==pBuf->count && pBuf->count>0)
		pBuf->isLastLineBlank = (pBuf->lines[pBuf->count-1].lineType==ilt_whitespace);

	for (int i=0; i<iEnd; i++) {
		int lineLen = bgstrlen(pBuf->lines[i].line);
		if (lineLen>0) {
			pBuf->lines[i].line[lineLen] = '\n';
			fwrite(pBuf->lines[i].line, 1, lineLen+1, tmpFD);
			pBuf->lines[i].line[lineLen] = '\0';
		} else
			fwrite("\n", 1,1, tmpFD);
	}

	// 012345678901234567890123456789
	// ++++++++++++---     iEnd==12  numUnconsumedLines==3
	// xxx+++++++++___
	//    or
	// +++-----            iEnd==3  numUnconsumedLines==5
	// xxx--___
	if (iEnd>0 && iEnd < pBuf->count) {
		int numUnconsumedLines = pBuf->count - iEnd;
		for (int i=0; i<min(numUnconsumedLines, iEnd); i++)
			IniLine_free(&(pBuf->lines[i]));
		memmove(pBuf->lines, &(pBuf->lines[iEnd]), numUnconsumedLines*sizeof(IniLine));
		memset(&(pBuf->lines[max(numUnconsumedLines,iEnd)]), 0,  (pBuf->count-max(numUnconsumedLines,iEnd))*sizeof(IniLine));
	}

	pBuf->count = pBuf->count - iEnd;
}


void IniLineBuffer_insertSetting(IniLineBuffer** ppBuf, IniScheme* pScheme, char* name, char* value)
{
	IniLineBuffer* pBuf = *ppBuf;
	//           1         2
	// 012345678901234567890123456789
	// 0      *     $
	// +++++++------
	int iEnd = pBuf->count-1;
	if (iEnd>=0 && pBuf->lines[iEnd].lineType==ilt_section) iEnd--;
	while (iEnd>=0 && ! LineType_isData(pBuf->lines[iEnd].lineType)) iEnd--;
	iEnd++;

	// iEnd now points to the first line that is past the end of the current section

	// // if the first line past the end of the section is a blank line, lets reuse it (because if there is sufficient space already,
	// // we dont want to add more space)
	// if (iEnd<pBuf->count && pBuf->lines[iEnd].lineType == ilt_whitespace) iEnd++;

	int hasBeforeComment = (pScheme && pScheme->comment && pScheme->commentsStyle && strcmp("BEFORE", pScheme->commentsStyle)==0);

	// now determine if we have to insert a leading or trailing blank line
	int leadingBlank  = (!!hasBeforeComment) || (iEnd>=2 && pBuf->lines[(iEnd-1)].lineType==ilt_setting  && pBuf->lines[(iEnd-2)].lineType==ilt_comment);
	int trailingBlank = (iEnd<pBuf->count && pBuf->lines[(iEnd)].lineType != ilt_whitespace) ? 1:0;

	int insertCount = 1+leadingBlank+trailingBlank + hasBeforeComment;
	IniLineBuffer_allocateForAdditionalLines(&pBuf, insertCount);

	// 012345678901234567890123456789
	// +++=++++____     iEnd==3  count==8  numEndLines==5  insertCount==3
	// +++iii=++++_
	//    CCC  DDD
	//   or
	// 012345678901234567890123456789
	// +++++++=____     iEnd==7  count==8  numEndLines==1  insertCount==3
	// +++++++iii=_
	//        C__D
	if ((iEnd) < pBuf->count) {
		int numEndLines = pBuf->count - iEnd;
		for (int i=max(pBuf->count, iEnd+insertCount); i<min(insertCount, pBuf->count-iEnd); i++)
			IniLine_free(&(pBuf->lines[i]));
		memmove(&(pBuf->lines[iEnd+insertCount]), &(pBuf->lines[iEnd]), numEndLines*sizeof(IniLine));
		memset(&(pBuf->lines[iEnd]), 0, sizeof(IniLine) * min(insertCount,pBuf->count-iEnd));
	}
	pBuf->count = pBuf->count + insertCount;

//__bgtrace("  inserting '%d' '%d' '%d' '%d'\n", leadingBlank, hasBeforeComment, 1, trailingBlank);

	if (leadingBlank)
		IniLine_setToWhitespace(&(pBuf->lines[iEnd++]));

	if (hasBeforeComment)
		IniLine_setToComment(&(pBuf->lines[iEnd++]), pScheme, pScheme->comment);

	IniLine_setToSetting((pBuf->lines+iEnd++), pScheme, name, value);

	if (trailingBlank)
		IniLine_setToWhitespace(&(pBuf->lines[iEnd++]));
}

// if the targetSection was not present in the file, this function will be called after the last chunk from the file is read into
// the line buffer.
void IniLineBuffer_appendSectionAndSetting(IniLineBuffer* pBuf, IniScheme* pScheme, char* targetSection, char* targetName, char* targetValue)
{
	// if targetSection is "" or "." its the top section which has no header
	int needsSectionHeader = (targetSection && *targetSection!='\0' && (*targetSection!='.' || *(targetSection+1)!='\0' ));

	// add a blank line before the new section only if the last line is not already whitespace and we are really adding a header
	if (needsSectionHeader && !IniLineBuffer_isLastLineWhitespace(pBuf))
		IniLine_setToWhitespace(IniLineBuffer_next(&pBuf));

	// section comment if called for. The default style for section comments is BEFORE
	if (pScheme->sectComment && (!pScheme->commentsStyle || strcmp("EOL", pScheme->commentsStyle)!=0))
		IniLine_setToComment(IniLineBuffer_next(&pBuf), pScheme, pScheme->sectComment);

	// add the "[ <targetSection> ]" line and the setting line
	if (needsSectionHeader)
		IniLine_setToSection(IniLineBuffer_next(&pBuf), pScheme, targetSection);

	// setting comment if called for. The default style for setting comments is EOL
	if (pScheme->comment && pScheme->commentsStyle && strcmp("BEFORE", pScheme->commentsStyle)==0)
		IniLine_setToComment(IniLineBuffer_next(&pBuf), pScheme, pScheme->comment);

	IniLine_setToSetting(IniLineBuffer_next(&pBuf), pScheme, targetName, targetValue);
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// misc


void iniValidate(char* schema, char* value)
{
	char* pS = schema;
	char* schemaType=pS;

	while (*pS && *pS!=':') pS++;
	if (*pS)
		*pS = '\0';
	char* schemaData = pS;

	regex_t* regex = NULL;
	if (       strcmp("enum", schemaType)) {
		static regex_t regex_enum; regcomp(&regex_enum, "^(${schemaData//[ :,$'\t']/|})$", REG_EXTENDED);
		regex = &regex_enum;
	} else if (strcmp("int",  schemaType)) {
		static regex_t regex_int; regcomp(&regex_int, "^[-+0-9]*$", REG_EXTENDED);
		regex = &regex_int;
	} else if (strcmp("real", schemaType)) {
		static regex_t regex_real; regcomp(&regex_real, "^[-+0-9.]*$", REG_EXTENDED);
		regex = &regex_real;
	} else {
		assertError(NULL, "unknown schema type.\n\tschema='%s'\n\ttype='%s'\n\tdata='%s'\n", schema, schemaType, schemaData);
	}

	if (regexec(regex, value, 0, NULL, 0) != 0) {
		assertError(NULL, "Config data validation failed.\n\tschema='%s'\n\tvalue='%s'\n", schema, value);
	}
}

void IniScheme_init(IniScheme* pScheme)
{
	// 0's are the default for most of the fields
	memset(pScheme, 0, sizeof(*pScheme));
	pScheme->delim     = "=";
	pScheme->sectPad   = " ";
	pScheme->paramPad  = "";
	pScheme->quoteMode = "";
}

void IniScheme_validate(IniScheme* pScheme)
{
	if (!pScheme->quoteMode) assertError(NULL, "IniScheme_validate: 'quoteMode' should not be null");
	if (!pScheme->sectPad)   assertError(NULL, "IniScheme_validate: 'sectPad' should not be null");
	if (!pScheme->paramPad)  assertError(NULL, "IniScheme_validate: 'paramPad' should not be null");

	for (char* s=pScheme->sectPad; *s; s++) if (!whitespace(*s))
		assertError(NULL, "IniScheme_validate: --sectPad='%s' option must contain only spaces or tabs", pScheme->sectPad);
	for (char* s=pScheme->paramPad; *s; s++) if (!whitespace(*s))
		assertError(NULL, "IniScheme_validate: --paramPad='%s' option must contain only spaces or tabs", pScheme->paramPad);

	if ((*pScheme->quoteMode && *pScheme->quoteMode!='"' && *pScheme->quoteMode!='\'') ||  strlen(pScheme->quoteMode)>1 )
		assertError(NULL, "IniScheme_validate: 'quoteMode'(%s) must be empty, a single quote, or a double quote", pScheme->quoteMode);

	if (pScheme->commentsStyle
		&&  strcmp("EOL", pScheme->commentsStyle)!=0
		&&  strcmp("BEFORE", pScheme->commentsStyle)!=0
		&&  strcmp("NONE", pScheme->commentsStyle)!=0)
			assertError(NULL, "IniScheme_validate: --commentsStyle='%s' option must be one of NONE,BEFORE,EOL", pScheme->commentsStyle);

	//pScheme->schema;
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ini file operations

int iniParamGet(WORD_LIST* args)
{
	IniScheme iniScheme; IniScheme_init(&iniScheme);
	while (args && *args->word->word=='-') {
		char* optArg;
		if      ((optArg=BGCheckOpt("-R*|--retVar=*",        &args))) iniScheme.retVar           = optArg;
		else if ((optArg=BGCheckOpt("-t|--expandAsTemplate", &args))) iniScheme.expandAsTemplate = 1;
		else if ((optArg=BGCheckOpt("-a|--addIfNotFound",    &args))) iniScheme.addIfNotFound    = 1;

		// because of the --addIfNotFound option, iniParamGet accepts all the options of iniParamSet
		else if ((optArg=BGCheckOpt("-p|--mkdir",        &args))) iniScheme.mkdirFlag       = 1;
		else if ((optArg=BGCheckOpt("-S*|--statusVar*",  &args))) iniScheme.statusVarName   = optArg;
		else if ((optArg=BGCheckOpt("--resultsVar*",     &args))) iniScheme.resultsVarName  = optArg;
		else if ((optArg=BGCheckOpt("-c*|--comment*",    &args))) iniScheme.comment         = optArg;
		else if ((optArg=BGCheckOpt("--sectionComment*", &args))) iniScheme.sectComment     = optArg;
		else if ((optArg=BGCheckOpt("-qN|--noQuote",     &args))) iniScheme.quoteMode       = "none";
		else if ((optArg=BGCheckOpt("-q1|--singleQuote", &args))) iniScheme.quoteMode       = "single";
		else if ((optArg=BGCheckOpt("-q2|--doubleQuote", &args))) iniScheme.quoteMode       = "double";
		else if ((optArg=BGCheckOpt("-d*|--delim*",      &args))) iniScheme.delim           = optArg;
		else if ((optArg=BGCheckOpt("--sectPad*",        &args))) iniScheme.sectPad         = optArg;
		else if ((optArg=BGCheckOpt("--paramPad*",       &args))) iniScheme.paramPad        = optArg;
		else if ((optArg=BGCheckOpt("-x|--noSectionPad", &args))) iniScheme.sectPad         = "";
		else if ((optArg=BGCheckOpt("--commentsStyle*",  &args))) iniScheme.commentsStyle   = optArg;
		else if ((optArg=BGCheckOpt("-v*|--schema*",     &args))) iniScheme.schema          = optArg;
		args = args->next;
	}

	char* iniFilenameSpec = WordList_shift(&args);
	char* targetSection   = WordList_shift(&args);
	char* targetName      = WordList_shift(&args);
	char* defValue        = WordList_shift(&args);

	char* value = iniParamGetC(&iniScheme, iniFilenameSpec, targetSection, targetName, defValue);

	// the default for the C form of this function is to simply return value from the function but for the bash compatible form
	// we need the default destination to write it to std out
	if (!iniScheme.retVar) {
		printf("%s\n",bgstr(value));
	}

	xfree(value);
	return iniScheme.exitCode;
}

char* iniParamGetC(IniScheme* pScheme, char* iniFilenameSpec, char* targetSection, char* targetName, char* defValue)
{
	DefaultOpts(IniScheme, pScheme);
	IniScheme_validate(pScheme);

	// expand the iniFilenameSpec into a list of 0 or more existing files
	// we may not need to do this b/c the loop efficiently skips files that do not exist, but the bash implementation needs to
	// use fsExpandFiles to protect the file list sent to awk so we do the same so that they behaive the same.
	WORD_LIST* iniFilenameList = WordList_fromString(iniFilenameSpec, ", ",0);
	WORD_LIST* ipg_iniFiles = fsExpandFilesC(
		iniFilenameList,
		NULL,         // no BGRetVar means to return a WORD_LIST*
		NULL,         // no prefix to remove
		ef_filesOnly, // flags (ef_force,ef_recurse,ef_filesOnly,ef_foldersOnly)
		NULL,         // no findOpts
		NULL,         // no global find expressions
		NULL,         // no find expressions
		NULL,         // no gitignore file
		NULL,         // no exclude paths
		NULL);        // we dont need the separate found result b/c we are not using ef_force


	// the whole point of this function is to find and return this value
	char* targetValue = NULL;

	// the line buffer inside iniLine will hold the current line as we scan through files. freadline will grow it as needed to
	// accomadate the largest line encountered. the other fields will describe the parsed line contents
	IniLine iniLine = {0};

	// we loop through the files in order and then through the lines of each file in order so that if we find a matching
	// [section]name, we can stop looking b/c even if there are multiple, the first is the one we should return.
	for (WORD_LIST* oneFile=ipg_iniFiles; !targetValue && oneFile; oneFile=oneFile->next) {
		FILE* iniFileFD = fopen(oneFile->word->word, "r");
		if (!iniFileFD)
			continue;

		int inSect = (!targetSection || *targetSection=='\0' || (*(targetSection+1)=='\0' && (*targetSection=='.') ) );
		while (!targetValue && IniLine_read(&iniLine, pScheme, iniFileFD)) {
			switch (iniLine.lineType) {
				case ilt_comment: break;
				case ilt_whitespace: break;
				case ilt_invalid: break;
				case ilt_section:
					inSect = IniLine_isSectionEqTo(&iniLine, targetSection);
					break;
				case ilt_setting:
					if (inSect && IniLine_isParamNameEqTo(&iniLine, targetName)) {
						targetValue = savestringn(iniLine.paramValue, iniLine.paramValueLen);
					}
					break;
			}
		}

		// finished with this file
		fclose(iniFileFD);
	}

	// did we find it?
	pScheme->exitCode = (!targetValue);

	int wasNotFound = 0;
	if (!targetValue) {
		wasNotFound = 1;
		targetValue = (defValue) ? savestring(defValue) : NULL;
	}

	if (pScheme->expandAsTemplate && targetValue) {
		char* tmp = targetValue;
		targetValue = templateExpandStrC(NULL, targetValue, NULL);
		xfree(tmp);
	}

	if (pScheme->schema && targetValue)
		iniValidate(pScheme->schema, targetValue);

	if (wasNotFound && pScheme->addIfNotFound)
		iniParamSetC(pScheme, iniFilenameList->word->word, targetSection, targetName, bgstr(defValue));

	if (pScheme->retVar) {
		SHELL_VAR* vRetVar = ShellVar_findUpVar(pScheme->retVar);
		ShellVar_set(vRetVar, bgstr(targetValue));
	}

	IniLine_free(&iniLine);
	return targetValue;
}


void iniParamSetC(IniScheme* pScheme, char* iniFilename, char* targetSection, char* targetName, char* targetValue)
{
	//__bgtrace("############################# start iniParamSetC() ##############################\n");
	DefaultOpts(IniScheme, pScheme);
	IniScheme_validate(pScheme);

	if (pScheme->schema)
		iniValidate(pScheme->schema, targetValue);

	char* resultsType = "nochange";


	// the line buffer inside iniLine will hold the current line as we scan through files. freadline will grow it as needed to
	// accomadate the largest line encountered. the other fields will describe the parsed line contents
	IniLineBuffer* lineBuffer = NULL;
	IniLineBuffer_allocateForAdditionalLines(&lineBuffer, 20);
	IniLine* pIniLine;
	int found = 0;

	// the temp file that we will write out the new ini data to
	char* tmpName = mktempC("/tmp/bgIniParamSet.XXXXXXXXXX");
	FILE* tmpFD     = fopen(tmpName, "w");

	FILE* iniFileFD = fopen(iniFilename, "r");

	if (iniFileFD) {
		int inSect = (!targetSection || *targetSection=='\0' || (*(targetSection+1)=='\0' && (*targetSection=='.') ) );

		while (IniLine_read(pIniLine=IniLineBuffer_next(&lineBuffer), pScheme, iniFileFD)) {
			switch (pIniLine->lineType) {
				case ilt_section:
					// if we are ending the targetSection and did not find the setting, append it to the end of the section now.
					if ( !found && inSect) {
						found = 1;
						resultsType = "addedToExistingSection";
						IniLineBuffer_insertSetting(&lineBuffer, pScheme, targetName, targetValue);
					}
					inSect = IniLine_isSectionEqTo(pIniLine, targetSection);
					IniLineBuffer_flushSection(lineBuffer, tmpFD);
					break;

				case ilt_setting:
					if ( !found && inSect && IniLine_isParamNameEqTo(pIniLine, targetName)) {
						found = 1;
						resultsType = "changedExistingSetting";

						// if the value is different or we are adding/changing quoteMode or adding/changing the comment
						if (!IniLine_isParamValueEqTo(pIniLine, targetValue)
							|| (pScheme->quoteMode && strcmp(pScheme->quoteMode, bgstr(pIniLine->paramQuoteMode))!=0)
							|| (pScheme->comment && strcmp(bgstr(pScheme->commentsStyle),"BEFORE")!=0 && (strcmp(pScheme->comment, bgstr(pIniLine->comment))!=0) ) ) {

							IniLine_updateSettingValue(pIniLine, pScheme, targetName, targetValue);
						}
					}
					break;

				case ilt_comment:
				case ilt_whitespace:
				case ilt_invalid:
					break;
			}
		}
		// when EOF is reached it adds an invalid IniLine to the buffer so remove it
		lineBuffer->count--;

		if ( !found && inSect) {
			found = 1;
			resultsType = "addedToExistingSection";
			IniLineBuffer_insertSetting(&lineBuffer, pScheme, targetName, targetValue);
		}

		fclose(iniFileFD); iniFileFD = NULL;

		if (!found)
			resultsType = "addedSectionAndSetting";
	} else {
		// if the file we are modifying does not yet exist
		resultsType = "createdNewFile";
	}

	// if it was not found in the existing file (maybe b/c the file does not yet exist)
	if (!found) {
		IniLineBuffer_appendSectionAndSetting(lineBuffer, pScheme, targetSection, targetName, targetValue);
	}
	IniLineBuffer_flushAll(lineBuffer, tmpFD);
	fclose(tmpFD);

	int wasChanged = fsReplaceIfDifferent(tmpName, iniFilename, cp_removeSrc | ((pScheme->mkdirFlag)?cp_mkdir:0));

	xfree(tmpName);

	IniLineBuffer_free(lineBuffer);

	if (pScheme->statusVarName && wasChanged)
		ShellVar_setS(pScheme->statusVarName, "changed");

	if (pScheme->resultsVarName)
		ShellVar_setS(pScheme->resultsVarName, resultsType);

	return;
}

int iniParamSet(WORD_LIST* args)
{
	IniScheme iniScheme; IniScheme_init(&iniScheme);
	while (args && *args->word->word=='-') {
		char* optArg;
		if      ((optArg=BGCheckOpt("-p|--mkdir",        &args)))  iniScheme.mkdirFlag      = 1;
		else if ((optArg=BGCheckOpt("-S*|--statusVar*",  &args)))  iniScheme.statusVarName  = optArg;
		else if ((optArg=BGCheckOpt("-R*|--resultsVar*", &args)))  iniScheme.resultsVarName = optArg;
		else if ((optArg=BGCheckOpt("-c*|--comment*",    &args)))  iniScheme.comment        = optArg;
		else if ((optArg=BGCheckOpt("--sectionComment*", &args)))  iniScheme.sectComment    = optArg;
		else if ((optArg=BGCheckOpt("-qN|--noQuote",     &args)))  iniScheme.quoteMode      = "";
		else if ((optArg=BGCheckOpt("-q1|--singleQuote", &args)))  iniScheme.quoteMode      = "'";
		else if ((optArg=BGCheckOpt("-q2|--doubleQuote", &args)))  iniScheme.quoteMode      = "\"";
		else if ((optArg=BGCheckOpt("-d*|--delim*",      &args)))  iniScheme.delim          = optArg;
		else if ((optArg=BGCheckOpt("--sectPad*",        &args)))  iniScheme.sectPad        = optArg;
		else if ((optArg=BGCheckOpt("--paramPad*",       &args)))  iniScheme.paramPad       = optArg;
		else if ((optArg=BGCheckOpt("-x|--noSectionPad", &args)))  iniScheme.sectPad        = "";
		else if ((optArg=BGCheckOpt("--commentsStyle*",  &args)))  iniScheme.commentsStyle  = optArg;
		else if ((optArg=BGCheckOpt("-v*|--schema*",     &args)))  iniScheme.schema         = optArg;
		else assertError(NULL, "iniParamSet: unknown option '%s'",args->word->word);
		args = args->next;
	}

	char* iniFilenameSpec = WordList_shift(&args);
	char* targetSection   = WordList_shift(&args);
	char* targetName      = WordList_shift(&args);
	char* value           = WordList_shift(&args);

	iniParamSetC(&iniScheme, iniFilenameSpec, targetSection, targetName, value);

	return iniScheme.exitCode;
}



char* parserFindEndOfValue(char* pS)
{
	char* pE = pS;
	if (*pS == '"') {
		pE++;
		while (*pE && ! (*pE=='"' && *(pE-1)!='\\') ) pE++;
		if (*pE=='"') pE++;
	} else if (*pS == '\'') {
		pE++;
		while (*pE && ! (*pE=='\'' && *(pE-1)!='\\') ) pE++;
		if (*pE=='\'') pE++;
	} else if (*pS != '\0') {
		pE++;
		while (*pE && *pE!='#' ) pE++;
		while ((pE>pS) && whitespace(*(pE-1)) ) pE--;
	}
	return pE;
}


char* _configGetScopedFilesList()
{
	SHELL_VAR* vConfigScopes = ShellVar_find("configScopes");
	if (!vConfigScopes)
		assertError(NULL, "Could not find the 'configScopes' global array for the global config system");
	char* list = ShellVar_assocGet(vConfigScopes, "orderedFileList");
	if (!list) {
		BGString fileList; BGString_init(&fileList, 20);
		BGString orderedScopes; BGString_initFromStr(&orderedScopes, ShellVar_assocGet(vConfigScopes, "0"));
		BGString_replaceWhitespaceWithNulls(&orderedScopes);
		char* scopeName;
		while ((scopeName = BGString_nextWord(&orderedScopes))) {
			char* scopePath = ShellVar_assocGet(vConfigScopes, scopeName);
			if (!scopePath)
				assertError(NULL, "the 'configScopes' global associative array for the global config system is missing the entry for the scope '%s' which is mentioned in the element [0] ordered scope list", scopeName);
			BGString_append(&fileList, scopePath, ",");
		}
		ShellVar_assocSet(vConfigScopes, "orderedFileList", fileList.buf);
		list = ShellVar_assocGet(vConfigScopes, "orderedFileList");
		BGString_free(&orderedScopes);
		BGString_free(&fileList);
	}
	return list;
}

char* configGetC(IniScheme* pScheme, char* targetSection, char* targetName, char* defValue)
{
	return iniParamGetC(pScheme, _configGetScopedFilesList(), targetSection, targetName, defValue);
}

int   configGet(WORD_LIST* args)
{
	IniScheme iniScheme; IniScheme_init(&iniScheme);
	char* _csScope = NULL;
	while (args && *args->word->word=='-') {
		char* optArg;
		if      ((optArg=BGCheckOpt("-s*|--scope*",          &args))) _csScope                   = optArg;
		else if ((optArg=BGCheckOpt("-R*|--retVar=*",        &args))) iniScheme.retVar           = optArg;
		else if ((optArg=BGCheckOpt("-t|--expandAsTemplate", &args))) iniScheme.expandAsTemplate = 1;
		else if ((optArg=BGCheckOpt("-a|--addIfNotFound",    &args))) iniScheme.addIfNotFound    = 1;

		// because of the --addIfNotFound option, configGet accepts all the options of iniParamSet
		else if ((optArg=BGCheckOpt("-p|--mkdir",        &args))) iniScheme.mkdirFlag       = 1;
		else if ((optArg=BGCheckOpt("-S*|--statusVar*",  &args))) iniScheme.statusVarName   = optArg;
		else if ((optArg=BGCheckOpt("--resultsVar*",     &args))) iniScheme.resultsVarName  = optArg;
		else if ((optArg=BGCheckOpt("-c*|--comment*",    &args))) iniScheme.comment         = optArg;
		else if ((optArg=BGCheckOpt("--sectionComment*", &args))) iniScheme.sectComment     = optArg;
		else if ((optArg=BGCheckOpt("-qN|--noQuote",     &args))) iniScheme.quoteMode       = "none";
		else if ((optArg=BGCheckOpt("-q1|--singleQuote", &args))) iniScheme.quoteMode       = "single";
		else if ((optArg=BGCheckOpt("-q2|--doubleQuote", &args))) iniScheme.quoteMode       = "double";
		else if ((optArg=BGCheckOpt("-d*|--delim*",      &args))) iniScheme.delim           = optArg;
		else if ((optArg=BGCheckOpt("--sectPad*",        &args))) iniScheme.sectPad         = optArg;
		else if ((optArg=BGCheckOpt("--paramPad*",       &args))) iniScheme.paramPad        = optArg;
		else if ((optArg=BGCheckOpt("-x|--noSectionPad", &args))) iniScheme.sectPad         = "";
		else if ((optArg=BGCheckOpt("--commentsStyle*",  &args))) iniScheme.commentsStyle   = optArg;
		else if ((optArg=BGCheckOpt("-v*|--schema*",     &args))) iniScheme.schema          = optArg;
		args = args->next;
	}

	char* targetSection   = WordList_shift(&args);
	char* targetName      = WordList_shift(&args);
	char* defValue        = WordList_shift(&args);

	char* iniFilenameSpec;
	if (_csScope) {
		iniFilenameSpec = ShellVar_assocGetS("configScopes", _csScope);
		if (!iniFilenameSpec)
			assertError(NULL, "The the scope name (%s) specified in the '-s|--scope=' option does not exist in the global associative array 'configScopes'", _csScope);
	} else
		iniFilenameSpec = _configGetScopedFilesList();

	char* value = iniParamGetC(&iniScheme, iniFilenameSpec, targetSection, targetName, defValue);

	// the default for the C form of this function is to simply return value from the function but for the bash compatible form
	// we need the default destination to write it to std out
	if (!iniScheme.retVar && value) {
		printf("%s\n",value);
	}

	xfree(value);
	return iniScheme.exitCode;
}
