
#include "bg_bashAPI.h"

SHELL_VAR* ShellVar_findOrCreate(char* varname)
{
    SHELL_VAR* var = find_variable(varname);
    if (!var)
        var = ShellVar_create(varname);
    return var;
}

SHELL_VAR* ShellVar_refCreate(char* varname)
{
    SHELL_VAR* var = make_local_variable(varname,0);
    VSETATTR(var, att_nameref);
    return var;
}

SHELL_VAR* ShellVar_refCreateSet(char* varname, char* value)
{
    SHELL_VAR* var = make_local_variable(varname,0);
    VSETATTR(var, att_nameref);
    bind_variable_value(var,value,0);
    return var;
}

// this does not use array_to_word_list() because the WORD_LIST used in arrays dont use the same allocation as in the rest
WORD_LIST* ShellVar_arrayToWordList(SHELL_VAR* var)
{
    WORD_LIST* ret=NULL;
    for (ARRAY_ELEMENT* el = (array_cell(var))->lastref->prev; el!=(array_cell(var))->head; el=el->prev ) {
        ret = make_word_list(make_word(el->value), ret);
    }
    return ret;
}


SHELL_VAR* ShellVar_findWithSuffix(char* varname, char* suffix)
{
    char* buf = xmalloc(strlen(varname)+strlen(suffix)+1);
    strcpy(buf, varname);
    strcat(buf,suffix);
    SHELL_VAR* vVar = find_variable(buf);
    xfree(buf);
    return vVar;
}


void ShellVar_assocCopyElements(HASH_TABLE* dest, HASH_TABLE* source)
{
    register int i;
    BUCKET_CONTENTS *item;
    if (source != 0 && HASH_ENTRIES (source) != 0) {
        for (i = 0; i < source->nbuckets; i++) {
            for (item = hash_items (i, source); item; item = item->next) {
                assoc_insert(dest, savestring(item->key), item->data);
            }
        }
    }
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// WordList
// native WORD_LIST functions are mostly in make_cmd.h

void WordList_unshift(WORD_LIST* list, char* str)
{
    WORD_DESC* newWord = alloc_word_desc();
    newWord->word = list->word->word;
    newWord->flags = list->word->flags;
    WORD_LIST* wl = make_word_list(newWord, list->next);
    list->next = wl;
    list->word->word = savestring(str);
    make_word_flags(list->word, str);
}

char* WordList_shift(WORD_LIST* list)
{
    char* ret = savestring(list->word->word);
    WORD_LIST* temp = list;
    list = list->next;
    temp->next = NULL;
    dispose_words(temp);
    return ret;
}



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// AssocItr

BUCKET_CONTENTS* AssocItr_next(AssocItr* pI)
{
    // first iterate the current bucket linked list if we are not at the end
    if (pI->item && pI->item->next) {
        pI->item = pI->item->next;

    // next, goto the start linked list of the next non-empty bucket
    } else if (HASH_ENTRIES (pI->table) != 0 && pI->table && pI->position < pI->table->nbuckets) {
        while ((pI->position < pI->table->nbuckets) && 0==(pI->item=hash_items(pI->position, pI->table))) pI->position++;
        pI->position++;
    } else {
        pI->item = NULL;
    }
    return pI->item;
}
BUCKET_CONTENTS* AssocItr_init(AssocItr* pI, SHELL_VAR* vVar)
{
    pI->vVar=vVar;
    pI->table = assoc_cell(pI->vVar);
    pI->position=0;
    pI->item=NULL;
    pI->item = AssocItr_next(pI);
    return pI->item;
}
