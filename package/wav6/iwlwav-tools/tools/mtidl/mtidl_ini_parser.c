/******************************************************************************

                               Copyright (c) 2012
                            Lantiq Deutschland GmbH

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/
#include "mtlkinc.h"
#include "mtidl_ini_parser.h"
#include "dirent.h"
#include "iniparseraux.h"

#include <sys/stat.h>

#define LOG_LOCAL_GID   GID_MTIDLPARSER
#define LOG_LOCAL_FID   0

typedef BOOL (*_mtlk_enum_files_callback_t)(const char* file_name, mtlk_handle_t ctx);

#define MAX_FILE_NAME (1024)

/* ceiling(log10(2^32))  */
#define MAX_UINT_STRLEN (10)

typedef struct dict_cache_entry
{
  char                      *fname;
  dictionary                *dict;
  struct dict_cache_entry   *next;
} dict_cache_entry_t;

static dict_cache_entry_t dict_cache;

static BOOL _mtlk_is_dir(const char* path){

  struct stat sb;

  if (stat(path, &sb) == 0 && S_ISDIR(sb.st_mode))
  {
    return TRUE;
  }
  return FALSE;

}

static int
_mtlk_enum_files_by_extension(const char* dir,
                              const char* ext,
                              _mtlk_enum_files_callback_t func,
                              mtlk_handle_t ctx)
{
  struct dirent *dirp;
  int file_ext_len = strlen(ext);
  DIR *dp = opendir(dir);

  if(NULL == dp)
  {
    return MTLK_ERR_NO_ENTRY;
  }

  for(dirp = readdir(dp); NULL != dirp; dirp = readdir(dp))
  {
    int file_name_len = strlen(dirp->d_name);
    if((file_ext_len < file_name_len) &&
       (!strcmp(ext, &dirp->d_name[file_name_len - file_ext_len])))
    {
      char full_name[MAX_FILE_NAME];
      snprintf(full_name, MAX_FILE_NAME, "%s/%s", dir, dirp->d_name);
      func(full_name, ctx); /* we continue regardless of return code, in order to avoid linux memory leak
                               which occurs if not all directory entries are read out */
    }
  }

  closedir(dp);
  return MTLK_ERR_OK;
}

typedef struct __mtlk_enum_items_ctx_t
{
  int result;
  mtlk_enum_items_callback_f process_item_func;
  mtlk_handle_t func_ctx;
} _mtlk_enum_items_ctx_t;

typedef struct __mtlk_enum_fields_ctx_t
{
  dictionary * dict;
  const char* section_name;
} _mtlk_enum_fields_ctx_t;

int
dict_cache_init()
{
  memset(&dict_cache, 0, sizeof(dict_cache_entry_t));
  return MTLK_ERR_OK;
}

void
dict_cache_cleanup()
{
  dict_cache_entry_t *curr, *next;

  /* List starts with the dummy element */
  for (curr = dict_cache.next; curr; curr = next)
  {
    next = curr->next;
    free(curr->fname);
    iniparser_freedict(curr->dict);
    free(curr);
  }
}

static dict_cache_entry_t *
dict_cache_entry_alloc(const char* name)
{
  dict_cache_entry_t *new_node;
  dictionary *dic = iniparser_load(name);
  if (NULL == dic)
  {
      return NULL;
  }

  if (NULL == (new_node = malloc(sizeof(dict_cache_entry_t))))
  {
      iniparser_freedict(dic);
      return NULL;
  }

  if (NULL == (new_node->fname = strdup(name)))
  {
      iniparser_freedict(dic);
      free(new_node);
      return NULL;
  }

  new_node->dict = dic;
  new_node->next = NULL;

  return new_node;
}

static dictionary *
dict_cache_insert(const char *name)
{
  dict_cache_entry_t *current = &dict_cache;

  /* List starts with the dummy element */
  while (current->next)
  {
      if(!strcmp(current->next->fname, name))
      {
          return current->next->dict;
      }
      current = current->next;
  }

  current->next = dict_cache_entry_alloc(name);
  if (current->next == NULL)
  {
      return NULL;
  }

  return current->next->dict;
}

static BOOL
_mtlk_enum_items_in_file(const char* fname, mtlk_handle_t ctx)
{
  _mtlk_enum_items_ctx_t *enum_ctx = HANDLE_T_PTR(_mtlk_enum_items_ctx_t, ctx);

  dictionary * fdic = dict_cache_insert(fname);

  BOOL res = TRUE;
  unsigned i;

  if(NULL == fdic)
  {
    enum_ctx->result = MTLK_ERR_UNKNOWN;
    return FALSE;
  }

  for(i=0;;i++)
  {
    static const char ITEM_SECTION_PREFIX[]       = "mtidl_item_";
    static const char ITEM_FRIENDLY_NAME[]        = "friendly_name";
    static const char ITEM_DESCRIPTION_NAME[]     = "description";
    static const char ITEM_BINARY_TYPE_NAME[]     = "binary_type";
    static const char ITEM_TYPE_NAME[]            = "type";
    static const char ITEM_LEVEL_NAME[]           = "level";
    static const char ITEM_SOURCE_NAME[]          = "source";
    static const char ITEM_ID_NAME[]              = "id";
    static const char ITEM_OFFSET_NAME[]          = "offset";
    static const int  _MTLK_MTIDL_FIELD_NOT_FOUND = -1;

    char section_name[ARRAY_SIZE(ITEM_SECTION_PREFIX) + MAX_UINT_STRLEN + 1];
    mtlk_mtidl_item_t item;

    snprintf(section_name, ARRAY_SIZE(section_name), "%s%u", ITEM_SECTION_PREFIX, i);
    item.friendly_name = iniparser_aux_getstr(fdic, section_name, ITEM_FRIENDLY_NAME);

    if(NULL != item.friendly_name)
    {
      _mtlk_enum_fields_ctx_t enum_fields_ctx;

      item.description    = iniparser_aux_getstr(fdic, section_name, ITEM_DESCRIPTION_NAME);
      item.binary_type    = iniparser_aux_getstr(fdic, section_name, ITEM_BINARY_TYPE_NAME);
      item.type           = iniparser_aux_getint(fdic, section_name, ITEM_TYPE_NAME, _MTLK_MTIDL_FIELD_NOT_FOUND);
      item.provider_level = iniparser_aux_getint(fdic, section_name, ITEM_LEVEL_NAME, _MTLK_MTIDL_FIELD_NOT_FOUND);
      item.info_source    = iniparser_aux_getint(fdic, section_name, ITEM_SOURCE_NAME, _MTLK_MTIDL_FIELD_NOT_FOUND);
      item.info_id        = iniparser_aux_getint(fdic, section_name, ITEM_ID_NAME, _MTLK_MTIDL_FIELD_NOT_FOUND);
      item.offset         = iniparser_aux_getint(fdic, section_name, ITEM_OFFSET_NAME, 0);        /* 0 by default */

      if((NULL == item.description) ||
         (NULL == item.binary_type) ||
         (_MTLK_MTIDL_FIELD_NOT_FOUND == item.type) ||
         (_MTLK_MTIDL_FIELD_NOT_FOUND == item.provider_level) ||
         (_MTLK_MTIDL_FIELD_NOT_FOUND == item.info_source) ||
         (_MTLK_MTIDL_FIELD_NOT_FOUND == item.info_id)
        )
      {
        enum_ctx->result = MTLK_ERR_UNKNOWN;
        res = FALSE;
        break;
      }

      enum_fields_ctx.dict = fdic;
      enum_fields_ctx.section_name = section_name;
      item.fields_enum_ctx = HANDLE_T(&enum_fields_ctx);

      res = enum_ctx->process_item_func(&item, enum_ctx->func_ctx);
      if(!res) break;
    }
    else break;
  }

  return res;
}

int __MTLK_IFUNC
mtlk_mtidl_item_enum_fields(mtlk_handle_t fields_enum_ctx,
                            mtlk_enum_fields_callback_f callback,
                            mtlk_handle_t context)
{
  _mtlk_enum_fields_ctx_t * enum_ctx = HANDLE_T_PTR(_mtlk_enum_fields_ctx_t, fields_enum_ctx);
  int res = MTLK_ERR_OK;
  int i;

  for(i=0;;i++)
  {
    static const char INI_FIELD_NAME_TEMPLATE[]       = "field_%d_%s";
    static const char INI_FIELD_DESCRIPTION_NAME[]    = "description";
    static const char INI_FIELD_BINARY_TYPE_NAME[]    = "binary_type";
    static const char INI_FIELD_BINARY_SUBTYPE_NAME[] = "binary_subtype";
    static const char INI_FIELD_ELEMENT_SIZE[]        = "element_size";
    static const char INI_FIELD_FRACT_SIZE[]          = "fract_size";
    static const char INI_FIELD_NUM_ELEMENTS[]        = "num_elements";     /* total */
    static const char INI_FIELD_NUM_DIMENSIONS[]      = "num_dimensions";   /* 1 or 2 */
    static const char INI_FIELD_DIMENSION2[]          = "dimension2";       /* 2D array */
    static const char INI_FIELD_ITEM_NEXT_IDX_OFFS[]  = "next_idx_offs";    /* offset to next element of 1D array */
    static const int  _MTLK_MTIDL_FIELD_NOT_FOUND     = -1;
    static const int  _MAX_INI_FIELD_NAME             = 512;

    char ini_field_name[_MAX_INI_FIELD_NAME];
    mtlk_mtidl_field_t field;

    snprintf(ini_field_name, _MAX_INI_FIELD_NAME, INI_FIELD_NAME_TEMPLATE, i, INI_FIELD_DESCRIPTION_NAME);
    field.description = iniparser_aux_getstr(enum_ctx->dict, enum_ctx->section_name, ini_field_name);

    if(NULL != field.description)
    {
      snprintf(ini_field_name, _MAX_INI_FIELD_NAME, INI_FIELD_NAME_TEMPLATE, i, INI_FIELD_BINARY_TYPE_NAME);
      field.binary_type    = iniparser_aux_getstr(enum_ctx->dict, enum_ctx->section_name, ini_field_name);

      snprintf(ini_field_name, _MAX_INI_FIELD_NAME, INI_FIELD_NAME_TEMPLATE, i, INI_FIELD_BINARY_SUBTYPE_NAME);
      field.binary_subtype = iniparser_aux_getstr(enum_ctx->dict, enum_ctx->section_name, ini_field_name);

      snprintf(ini_field_name, _MAX_INI_FIELD_NAME, INI_FIELD_NAME_TEMPLATE, i, INI_FIELD_ELEMENT_SIZE);
      field.element_size   = iniparser_aux_getint(enum_ctx->dict, enum_ctx->section_name, ini_field_name, _MTLK_MTIDL_FIELD_NOT_FOUND);

      snprintf(ini_field_name, _MAX_INI_FIELD_NAME, INI_FIELD_NAME_TEMPLATE, i, INI_FIELD_FRACT_SIZE);
      field.fract_size   = iniparser_aux_getint(enum_ctx->dict, enum_ctx->section_name, ini_field_name, _MTLK_MTIDL_FIELD_NOT_FOUND);

      snprintf(ini_field_name, _MAX_INI_FIELD_NAME, INI_FIELD_NAME_TEMPLATE, i, INI_FIELD_NUM_ELEMENTS);
      field.num_elements   = iniparser_aux_getint(enum_ctx->dict, enum_ctx->section_name, ini_field_name, _MTLK_MTIDL_FIELD_NOT_FOUND);

      snprintf(ini_field_name, _MAX_INI_FIELD_NAME, INI_FIELD_NAME_TEMPLATE, i, INI_FIELD_NUM_DIMENSIONS);
      field.num_dimensions   = iniparser_aux_getint(enum_ctx->dict, enum_ctx->section_name, ini_field_name, 1);

      snprintf(ini_field_name, _MAX_INI_FIELD_NAME, INI_FIELD_NAME_TEMPLATE, i, INI_FIELD_DIMENSION2);
      field.dimension2   = iniparser_aux_getint(enum_ctx->dict, enum_ctx->section_name, ini_field_name, 1);

      snprintf(ini_field_name, _MAX_INI_FIELD_NAME, INI_FIELD_NAME_TEMPLATE, i, INI_FIELD_ITEM_NEXT_IDX_OFFS);
      field.next_idx_offs = iniparser_aux_getint(enum_ctx->dict, enum_ctx->section_name, ini_field_name, 0);

      if((NULL == field.binary_type) ||
         (NULL == field.binary_subtype) ||
         (_MTLK_MTIDL_FIELD_NOT_FOUND == field.element_size) ||
         (_MTLK_MTIDL_FIELD_NOT_FOUND == field.fract_size) ||
         (_MTLK_MTIDL_FIELD_NOT_FOUND == field.num_elements)
         )
      {
        res = MTLK_ERR_UNKNOWN;
        break;
      }

      res = callback(&field, context);
      if(MTLK_ERR_OK != res) break;
    }
    else break;
  }

  return res;
}

static const char MTIDL_EXTENSION[] = ".mtidlc";

int __MTLK_IFUNC
mtlk_mtidl_enum_items(const char* mtidl_path,
                      mtlk_enum_items_callback_f callback,
                      mtlk_handle_t context)
{
  int res = MTLK_ERR_OK;
  _mtlk_enum_items_ctx_t enum_items_ctx;
  enum_items_ctx.func_ctx = context;
  enum_items_ctx.process_item_func = callback;
  enum_items_ctx.result = MTLK_ERR_OK;

  if (_mtlk_is_dir(mtidl_path)){
    res = _mtlk_enum_files_by_extension(mtidl_path, MTIDL_EXTENSION, _mtlk_enum_items_in_file, HANDLE_T(&enum_items_ctx));
  }
  else{
    _mtlk_enum_items_in_file(mtidl_path, HANDLE_T(&enum_items_ctx));
    /* ignore return code */
  }

  if(MTLK_ERR_OK != res)
    return res;

  return enum_items_ctx.result;
}

typedef struct __mtlk_traverse_tree_ctx
{
  const char* mtidl_path;
  const char* binary_type;
  mtlk_tree_traversal_clb_f per_field_callback;
  mtlk_handle_t per_field_callback_ctx;

  uint32 depth_in_tree;
  int res;
} _mtlk_traverse_tree_ctx;

static BOOL
_mtlk_traverse_mtidl_subtree_on_item_clb(const mtlk_mtidl_item_t* item, mtlk_handle_t ctx);

static BOOL
_mtlk_mtidl_is_subitem_field(const mtlk_mtidl_field_t* field)
{
  return 0 == field->element_size;
}

static int
_mtlk_traverse_mtidl_subtree_on_field_clb(const mtlk_mtidl_field_t* field, mtlk_handle_t ctx)
{
  int res;
  _mtlk_traverse_tree_ctx *traverse_ctx = HANDLE_T_PTR(_mtlk_traverse_tree_ctx, ctx);

  /* Call per-field callback */
  traverse_ctx->per_field_callback(field,
                                   traverse_ctx->depth_in_tree,
                                   traverse_ctx->per_field_callback_ctx);

  /* If subitem - call traverse recursively */
  if(_mtlk_mtidl_is_subitem_field(field))
  {
    traverse_ctx->depth_in_tree++;
    traverse_ctx->binary_type = field->binary_type;
    traverse_ctx->res = MTLK_ERR_NO_ENTRY;

    res = mtlk_mtidl_enum_items(traverse_ctx->mtidl_path,
                                _mtlk_traverse_mtidl_subtree_on_item_clb,
                                ctx);
    traverse_ctx->depth_in_tree--;

    if(MTLK_ERR_OK == traverse_ctx->res)
      traverse_ctx->res = res;

    if(MTLK_ERR_OK != traverse_ctx->res)
    {
    }
  }
  else traverse_ctx->res = MTLK_ERR_OK;

  return traverse_ctx->res;
}

static BOOL
_mtlk_traverse_mtidl_subtree_on_item_clb(const mtlk_mtidl_item_t* item, mtlk_handle_t ctx)
{
  _mtlk_traverse_tree_ctx *traverse_ctx = HANDLE_T_PTR(_mtlk_traverse_tree_ctx, ctx);

  if(!strcmp(item->binary_type, traverse_ctx->binary_type))
  {
    mtlk_mtidl_field_t field;

    /* Call per-field callback */
    field.binary_type = traverse_ctx->binary_type;
    field.binary_subtype = "";
    field.description = item->description;
    field.element_size = 0;
    field.num_elements = 1;
    field.next_idx_offs = 0;

    traverse_ctx->per_field_callback(&field,
                                     traverse_ctx->depth_in_tree,
                                     traverse_ctx->per_field_callback_ctx);

    /* Enumerate fields */
    traverse_ctx->depth_in_tree++;
    traverse_ctx->res = mtlk_mtidl_item_enum_fields(item->fields_enum_ctx, _mtlk_traverse_mtidl_subtree_on_field_clb, ctx);
    traverse_ctx->depth_in_tree--;
    return FALSE;
  }

  return TRUE;
}

int __MTLK_IFUNC
mtlk_traverse_mtidl_subtree(const char* mtidl_path,
                            const char* root_binary_type,
                            mtlk_tree_traversal_clb_f per_field_callback,
                            mtlk_handle_t per_field_callback_ctx)
{
  _mtlk_traverse_tree_ctx ctx;
  int res;

  ctx.mtidl_path = mtidl_path;
  ctx.binary_type = root_binary_type;
  ctx.per_field_callback = per_field_callback;
  ctx.per_field_callback_ctx = per_field_callback_ctx;

  ctx.depth_in_tree = 0;
  ctx.res = MTLK_ERR_NO_ENTRY;

  res = mtlk_mtidl_enum_items(mtidl_path, _mtlk_traverse_mtidl_subtree_on_item_clb, HANDLE_T(&ctx));

  return (MTLK_ERR_OK == ctx.res) ? res : ctx.res;
}

typedef struct __mtlk_enum_values_ctx
{
  const char* binary_type;
  const char* mtidl_section_prefix;
  mtlk_enum_value_clb_f per_value_callback;
  mtlk_handle_t per_value_callback_ctx;
  int result;
} _mtlk_enum_values_ctx;

enum __mtlk_template
{
  MTLK_MTIDL_TEMPLATE_BITFIELD,
  MTLK_MTIDL_TEMPLATE_ENUM
};

static BOOL
_mtlk_enum_enumeratables_in_file(const char* fname, mtlk_handle_t ctx)
{
  _mtlk_enum_values_ctx *enum_ctx = HANDLE_T_PTR(_mtlk_enum_values_ctx, ctx);
  dictionary * fdic = dict_cache_insert(fname);
  unsigned section_length;
  char *section_name;
  BOOL res = TRUE;
  unsigned i;

  if(NULL == fdic)
  {
    enum_ctx->result = MTLK_ERR_UNKNOWN;
    return FALSE;
  }

  section_length = strlen(enum_ctx->mtidl_section_prefix) + MAX_UINT_STRLEN + 1;
  section_name = mtlk_osal_mem_alloc(section_length, MTLK_MEM_TAG_MTIDL);

  if(NULL == section_name)
  {
    enum_ctx->result = MTLK_ERR_NO_MEM;
    return FALSE;
  }

  for(i=0;;i++)
  {
    static const char ITEM_BINARY_TYPE_NAME[] = "binary_type";
    const char* enum_binary_type;

    snprintf(section_name, section_length, "%s%u", enum_ctx->mtidl_section_prefix, i);

    enum_binary_type = iniparser_aux_getstr(fdic, section_name, ITEM_BINARY_TYPE_NAME);

    if(NULL != enum_binary_type)
    {
      int j;

      if(strcmp(enum_binary_type, enum_ctx->binary_type))
        continue;
      else res = FALSE;

      for(j = 0;;j++)
      {
        static const char FIELD_NAME_TEMPLATE[]       = "field_%d_%s";
        static const char FIELD_VALUE_NAME[]          = "name";
        static const char FIELD_NAME_NAME[]           = "value";
        static const int  MAX_ENUM_VALUE_NAME         = 512;

        char enum_value_field_name[MAX_ENUM_VALUE_NAME];
        mtlk_mtidl_enum_value_t enum_value;

        snprintf(enum_value_field_name, MAX_ENUM_VALUE_NAME, FIELD_NAME_TEMPLATE, j, FIELD_VALUE_NAME);
        enum_value.name = iniparser_aux_getstr(fdic, section_name, enum_value_field_name);

        if(NULL != enum_value.name)
        {
          snprintf(enum_value_field_name, MAX_ENUM_VALUE_NAME, FIELD_NAME_TEMPLATE, j, FIELD_NAME_NAME);
          enum_value.value = iniparser_aux_getint(fdic, section_name, enum_value_field_name, 0);

          if(!enum_ctx->per_value_callback(&enum_value, enum_ctx->per_value_callback_ctx))
            break;
        }
        else break;
      }
      break;
    }
    else break;
  }

  mtlk_osal_mem_free(section_name);
  return res;
}

static int
_mtlk_mtidl_enum_enumeratable_values(const char* mtidl_path,
                                     const char* enumeratable_binary_type,
                                     const char* mtidl_section_prefix,
                                     mtlk_enum_value_clb_f per_value_callback,
                                     mtlk_handle_t per_value_callback_ctx)
{
  int res = MTLK_ERR_OK;
  _mtlk_enum_values_ctx ctx;
  ctx.binary_type = enumeratable_binary_type;
  ctx.mtidl_section_prefix = mtidl_section_prefix;
  ctx.per_value_callback = per_value_callback;
  ctx.per_value_callback_ctx = per_value_callback_ctx;
  ctx.result = MTLK_ERR_OK;

  if (_mtlk_is_dir(mtidl_path)){
    res = _mtlk_enum_files_by_extension(mtidl_path, MTIDL_EXTENSION, _mtlk_enum_enumeratables_in_file, HANDLE_T(&ctx));
  }else{
    _mtlk_enum_enumeratables_in_file(mtidl_path, HANDLE_T(&ctx));
    /* ignore return code */
  }

  if(MTLK_ERR_OK != res)
    return res;
  else return ctx.result;
}

int __MTLK_IFUNC
mtlk_mtidl_enum_bitfield_values(const char* mtidl_path,
                                const char* bitfield_binary_type,
                                mtlk_enum_value_clb_f per_value_callback,
                                mtlk_handle_t per_value_callback_ctx)
{
  return _mtlk_mtidl_enum_enumeratable_values(mtidl_path,
                                              bitfield_binary_type,
                                              "mtidl_bitfield_",
                                              per_value_callback,
                                              per_value_callback_ctx);
}

int __MTLK_IFUNC
mtlk_mtidl_enum_enumeration_values(const char* mtidl_path,
                                   const char* enum_binary_type,
                                   mtlk_enum_value_clb_f per_value_callback,
                                   mtlk_handle_t per_value_callback_ctx)
{
  return _mtlk_mtidl_enum_enumeratable_values(mtidl_path,
                                              enum_binary_type,
                                              "mtidl_enum_",
                                              per_value_callback,
                                              per_value_callback_ctx);
}
