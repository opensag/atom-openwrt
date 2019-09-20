/******************************************************************************

                               Copyright (c) 2012
                            Lantiq Deutschland GmbH

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/
#include "mtlkinc.h"
#include "mtlk_param_db.h"
#include "mtlk_card_selector.h"

#define LOG_LOCAL_GID  GID_PDB
#define LOG_LOCAL_FID   1

struct _mtlk_pdb_t {
  mtlk_pdb_value_t      *storage;
  /* sync objects */
  mtlk_osal_spinlock_t  db_lock;
  mtlk_atomic_t         usage_ref_cnt;
  uint32                num_of_items;
  param_db_module_id    module_id;
  MTLK_DECLARE_INIT_STATUS;
};


MTLK_INIT_STEPS_LIST_BEGIN(param_db)
  MTLK_INIT_STEPS_LIST_ENTRY(param_db, PARAM_DB_PREPARE_STRUCTURE)
  MTLK_INIT_STEPS_LIST_ENTRY(param_db, PARAM_DB_SET_UNINITIALIZED)
  MTLK_INIT_STEPS_LIST_ENTRY(param_db, PARAM_DB_PREPARE_SYNCHRONIZATION)
MTLK_INIT_INNER_STEPS_BEGIN(param_db)
MTLK_INIT_STEPS_LIST_END(param_db);


static void _wave_pdb_init_param(mtlk_pdb_value_t * obj, uint32 flags, mtlk_pdb_t* parent) {
  obj->flags = flags;
  obj->parent = parent;
}

static int _wave_pdb_prepare_structure (mtlk_pdb_t* obj) {

  MTLK_UNREFERENCED_PARAM(obj);
  /* Allocate data strucutre */

    return MTLK_ERR_OK;
}

static __INLINE void __wave_pdb_incref(mtlk_pdb_t* obj) {
  mtlk_osal_atomic_inc(&obj->usage_ref_cnt);
}

static __INLINE void __wave_pdb_decref(mtlk_pdb_t* obj) {
  MTLK_ASSERT(mtlk_osal_atomic_get(&obj->usage_ref_cnt) != 0);

  mtlk_osal_atomic_dec(&obj->usage_ref_cnt);
}

static int _wave_pdb_destroy_stored_params (mtlk_pdb_t * obj) {
  int i;

  for(i = 0; i < obj->num_of_items; i++) {

    if((PARAM_DB_TYPE_STRING == obj->storage[i].type ||
        PARAM_DB_TYPE_BINARY == obj->storage[i].type ||
        PARAM_DB_TYPE_MAC == obj->storage[i].type) ) {

      if (NULL != obj->storage[i].value.value_ptr) {
        mtlk_osal_mem_free(obj->storage[i].value.value_ptr);
        obj->storage[i].value.value_ptr = NULL;
      }

      mtlk_osal_lock_cleanup(&obj->storage[i].param_lock);
    }

    obj->storage[i].flags = PARAM_DB_VALUE_FLAG_UNINITIALIZED;
  }

  return MTLK_ERR_OK;
}

static int _wave_pdb_init_values_set(mtlk_pdb_t *obj, mtlk_pdb_initial_value const **initial_values)
{
  mtlk_pdb_initial_value *value = NULL;

  MTLK_ASSERT(NULL != obj);

  for(; (NULL != initial_values) && (NULL != (*initial_values)); initial_values++)
  {
    for (value = (*initial_values); (NULL != value) && (value->id != obj->num_of_items); value++)
    {
      MTLK_ASSERT(value->id < obj->num_of_items);

      obj->storage[value->id].type = value->type;
      obj->storage[value->id].flags = value->flag;
      obj->storage[value->id].size = value->size;

      ILOG6_DDDDP("id %d, type 0x%x, flag 0x%x, size %d, ptr %p",
                  value->id,
                  value->type,
                  value->flag,
                  value->size,
                  value->value);

      switch(value->type) {
        case PARAM_DB_TYPE_INT:
            mtlk_osal_atomic_set(&obj->storage[value->id].value.value_int,
                                 *((uint32 *) (value->value)));

            ILOG6_D("value %d", *((uint32 *) (value->value)));
          break;

        case PARAM_DB_TYPE_STRING:
            /*It's recommended to initialize STRING parameters
             * using maximum possible string length value + 1 (for zero)*/
            obj->storage[value->id].value.value_ptr =
                mtlk_osal_mem_alloc(value->size, MTLK_MEM_TAG_PARAM_DB);

            if(!obj->storage[value->id].value.value_ptr) {
              return MTLK_ERR_NO_MEM;
            }

            ILOG6_S("value %s", (char*)value->value);

            wave_strcopy(obj->storage[value->id].value.value_ptr,
                    (char*)value->value,
                    value->size);

            mtlk_osal_lock_init(&obj->storage[value->id].param_lock);
          break;

        case PARAM_DB_TYPE_BINARY:
        case PARAM_DB_TYPE_MAC:
            obj->storage[value->id].value.value_ptr = mtlk_osal_mem_alloc(value->size, MTLK_MEM_TAG_PARAM_DB);

            if(!obj->storage[value->id].value.value_ptr) {
              return MTLK_ERR_NO_MEM;
            }

            wave_memcpy(obj->storage[value->id].value.value_ptr,
                   value->size,
                   value->value,
                   value->size);

            mtlk_osal_lock_init(&obj->storage[value->id].param_lock);
          break;

        default:
          MTLK_ASSERT(!"Invalid type");
          break;
      }
    }
  }

  return MTLK_ERR_OK;
}

#ifdef CPTCFG_IWLWAV_LINDRV_HW_PCIEG5
static int
_wave_pdb_init_values_set_pcieg5 (mtlk_pdb_t * obj, mtlk_card_type_info_t hw_type_info)
{
int result;

  /* HW_TYPE WRX300 family */
  if (_mtlk_card_is_wrx300_family(hw_type_info)) {
    result = _wave_pdb_init_values_set(obj, wave_pdb_initial_values_pcieg5_wrx300);
  } else {
    /* HW_TYPE_WRX500 and HW_TYPE_HAPS70 */
    result = _wave_pdb_init_values_set(obj, wave_pdb_initial_values_pcieg5_wrx500);
  }
  return result;
}
#endif

#ifdef CPTCFG_IWLWAV_LINDRV_HW_PCIEG6
static int
_wave_pdb_init_values_set_pcieg6(mtlk_pdb_t * obj, mtlk_card_type_info_t hw_type_info)
{
  int result;

  /* Use on wrx514 the same values as on wrx300 */
  MTLK_STATIC_ASSERT(HW_TYPE_WRX300 == HW_TYPE_WRX514);

  if (hw_type_info == HW_TYPE_WRX300) {
    result = _wave_pdb_init_values_set(obj, wave_pdb_initial_values_pcieg6_wrx300);
  }
  else {
    /* HW_TYPE_WRX500 and HW_TYPE_HAPS70 */
    result = _wave_pdb_init_values_set(obj, wave_pdb_initial_values_pcieg6_wrx500);
  }
  return result;
}
#endif

static int _wave_pdb_fill_stored_params (mtlk_pdb_t * obj, mtlk_card_type_t hw_type, mtlk_card_type_info_t hw_type_info) {
  int i;
  int result = MTLK_ERR_OK;

  MTLK_ASSERT(NULL != obj);

  for (i = 0; i < obj->num_of_items; i++) {
    _wave_pdb_init_param(&obj->storage[i], PARAM_DB_VALUE_FLAG_UNINITIALIZED, obj);
  }

  switch (obj->module_id) {
    case PARAM_DB_MODULE_ID_RADIO:
      result = _wave_pdb_init_values_set(obj, wave_pdb_initial_values_radio);
      if (MTLK_ERR_OK == result) {
        /* do HW depended initialization */
        CARD_SELECTOR_START(hw_type)
          IF_CARD_PCIEG5(result = _wave_pdb_init_values_set_pcieg5(obj, hw_type_info););
          IF_CARD_PCIEG6(result = _wave_pdb_init_values_set_pcieg6(obj, hw_type_info););
        CARD_SELECTOR_END();
      }
    break;
    case PARAM_DB_MODULE_ID_CORE:
      result = _wave_pdb_init_values_set(obj, mtlk_pdb_initial_values);
    break;
    default:
    break;
  }

  if(MTLK_ERR_OK != result) {
    _wave_pdb_destroy_stored_params(obj);
  }

  return result;
}

static int _wave_pdb_prepare_sync (mtlk_pdb_t * obj) {

  mtlk_osal_atomic_set(&obj->usage_ref_cnt, 0);
  mtlk_osal_lock_init(&obj->db_lock);

  return MTLK_ERR_OK;
}

static int _mtlk_pdb_destroy_structure (mtlk_pdb_t * obj) {
  MTLK_UNREFERENCED_PARAM(obj);

  return MTLK_ERR_OK;
}

static int _wave_pdb_free_sync (mtlk_pdb_t * obj) {
  /* For debugging purposes we can check if there are non 0 reference counters */
  MTLK_ASSERT(mtlk_osal_atomic_get(&obj->usage_ref_cnt) == 0);

  mtlk_osal_lock_cleanup(&obj->db_lock);

  return MTLK_ERR_OK;
}

static void __MTLK_IFUNC
_wave_pdb_cleanup(mtlk_pdb_t* obj) {
  ILOG4_V(">>");
  MTLK_ASSERT(NULL != obj);

  MTLK_CLEANUP_BEGIN(param_db, MTLK_OBJ_PTR(obj))
    MTLK_CLEANUP_STEP(param_db, PARAM_DB_PREPARE_SYNCHRONIZATION, MTLK_OBJ_PTR(obj),
                        _wave_pdb_free_sync, (obj));
    MTLK_CLEANUP_STEP(param_db, PARAM_DB_SET_UNINITIALIZED, MTLK_OBJ_PTR(obj),
                        _wave_pdb_destroy_stored_params, (obj));
    MTLK_CLEANUP_STEP(param_db, PARAM_DB_PREPARE_STRUCTURE, MTLK_OBJ_PTR(obj),
                        _mtlk_pdb_destroy_structure, (obj));
  MTLK_CLEANUP_END(param_db, MTLK_OBJ_PTR(obj));
}

static int __MTLK_IFUNC
_wave_pdb_init(mtlk_pdb_t* obj, mtlk_card_type_t hw_type, mtlk_card_type_info_t hw_type_info) {
  ILOG4_V(">>");

  MTLK_ASSERT(NULL != obj);

  MTLK_INIT_TRY(param_db, MTLK_OBJ_PTR(obj))
    MTLK_INIT_STEP(param_db, PARAM_DB_PREPARE_STRUCTURE, MTLK_OBJ_PTR(obj),
                   _wave_pdb_prepare_structure, (obj));
    MTLK_INIT_STEP(param_db, PARAM_DB_SET_UNINITIALIZED, MTLK_OBJ_PTR(obj),
                   _wave_pdb_fill_stored_params, (obj, hw_type, hw_type_info));
    MTLK_INIT_STEP(param_db, PARAM_DB_PREPARE_SYNCHRONIZATION, MTLK_OBJ_PTR(obj),
                   _wave_pdb_prepare_sync, (obj));

  MTLK_INIT_FINALLY(param_db, MTLK_OBJ_PTR(obj))
  MTLK_INIT_RETURN(param_db, MTLK_OBJ_PTR(obj), _wave_pdb_cleanup, (obj))
}

void __MTLK_IFUNC
wave_pdb_delete (mtlk_pdb_t *obj)
{
  ILOG4_V(">>");
  if (obj) {
    _wave_pdb_cleanup(obj);

    if (obj->storage) {
      mtlk_osal_mem_free(obj->storage);
    }
    mtlk_osal_mem_free(obj);
  }
}

static const char *_wave_param_db_module_id_str[] = {
  "PARAM_DB_MODULE_ID_FIRST",
  "PARAM_DB_MODULE_ID_RADIO",
  "PARAM_DB_MODULE_ID_CORE",
  "PARAM_DB_MODULE_ID_LAST"
};

static const int _wave_param_db_storage_items[] = {
  0,                            /* for PARAM_DB_MODULE_ID_FIRST */
  PARAM_DB_RADIO_LAST_VALUE_ID, /* for PARAM_DB_MODULE_ID_RADIO */
  PARAM_DB_CORE_LAST_VALUE_ID,  /* for PARAM_DB_MODULE_ID_CORE  */
  0                             /* for PARAM_DB_MODULE_ID_LAST  */
};

static int _wave_param_db_module_id_check(param_db_module_id module_id)
{
  int res = MTLK_ERR_UNKNOWN;
  const char *id_str;

  id_str = _wave_param_db_module_id_str[module_id];

  if (module_id > PARAM_DB_MODULE_ID_FIRST && module_id < PARAM_DB_MODULE_ID_LAST) {
    ILOG1_S("module_id %s", id_str);
    res = MTLK_ERR_OK;
  }
  else {
    ELOG_D("unknown module_id %d", module_id);
    res = MTLK_ERR_PARAMS;
  }
  return res;
}


mtlk_pdb_t * __MTLK_IFUNC
wave_pdb_create(param_db_module_id module_id, mtlk_card_type_t hw_type, mtlk_card_type_info_t hw_type_info) {
  mtlk_pdb_t *param_db = NULL;
  mtlk_pdb_value_t *storage = NULL;
  uint32 param_db_items;

  ILOG4_V(">>");

  if (MTLK_ERR_OK != _wave_param_db_module_id_check(module_id))
    return NULL;

  param_db_items = _wave_param_db_storage_items[module_id];
  ILOG1_D("allocating storage for %d items", param_db_items);
  storage = (mtlk_pdb_value_t *)mtlk_osal_mem_alloc(sizeof(mtlk_pdb_value_t) * param_db_items, MTLK_MEM_TAG_PARAM_DB);
  if (NULL == storage)
    return NULL;

  param_db = (mtlk_pdb_t *)mtlk_osal_mem_alloc(sizeof(mtlk_pdb_t), MTLK_MEM_TAG_PARAM_DB);
  if(NULL == param_db) {
    mtlk_osal_mem_free(storage);
    return NULL;
  }

  memset(storage, 0, sizeof(mtlk_pdb_value_t) * param_db_items);
  memset(param_db, 0, sizeof(mtlk_pdb_t));

  param_db->storage = storage;
  param_db->num_of_items = param_db_items;
  param_db->module_id = module_id;

  if (MTLK_ERR_OK != _wave_pdb_init(param_db, hw_type, hw_type_info)) {
    mtlk_osal_mem_free(param_db);
    return NULL;
  }

  return param_db;
}

mtlk_pdb_handle_t __MTLK_IFUNC
wave_pdb_open(mtlk_pdb_t* obj, mtlk_pdb_id_t id) {

  mtlk_pdb_handle_t handle = NULL;

  ILOG4_V(">>");

  MTLK_ASSERT(NULL != obj);
  MTLK_ASSERT(id < obj->num_of_items);


  mtlk_osal_lock_acquire(&obj->db_lock);
  __wave_pdb_incref(obj);
  handle = &obj->storage[id];
  mtlk_osal_lock_release(&obj->db_lock);
  ILOG4_D("<< open fast handle id(%u) ", id);

  return handle;
}

void __MTLK_IFUNC
wave_pdb_close(mtlk_pdb_handle_t handle) {
  ILOG4_V(">>");
  /* Perform debug\statistics - for example:
     if we keep per value reference counts, check them here */
  mtlk_osal_lock_acquire(&handle->parent->db_lock);
  __wave_pdb_decref(handle->parent);
  mtlk_osal_lock_release(&handle->parent->db_lock);

  ILOG4_V("<< close fast handle");
}

int __MTLK_IFUNC
wave_pdb_get_int(mtlk_pdb_t* obj, mtlk_pdb_id_t id)
{
  int value;
  mtlk_pdb_handle_t handle;

  ILOG4_V(">>");

  handle = wave_pdb_open(obj, id);
  value = mtlk_pdb_fast_get_int(handle);
  wave_pdb_close(handle);

  ILOG3_DD("<< get param id(%u) value(%d)", id, value);
  return value;
}

int __MTLK_IFUNC
wave_pdb_get_string(mtlk_pdb_t* obj, mtlk_pdb_id_t id, char * value, mtlk_pdb_size_t * size) {

  int result = MTLK_ERR_OK;
  mtlk_pdb_handle_t handle;

  ILOG4_V(">>");

  handle = wave_pdb_open(obj, id);
  result = mtlk_pdb_fast_get_string(handle, value, size);
  wave_pdb_close(handle);

  ILOG3_D("<< exit code - %d", result);
  return result;
}

int __MTLK_IFUNC
wave_pdb_get_binary(mtlk_pdb_t* obj, mtlk_pdb_id_t id, void * buffer, mtlk_pdb_size_t * size) {

  int result = MTLK_ERR_UNKNOWN;
  mtlk_pdb_handle_t handle;

  ILOG4_V(">>");

  memset(buffer, 0, *size);
  handle = wave_pdb_open(obj, id);
  if (handle) {
    result = mtlk_pdb_fast_get_binary(handle, buffer, size);
    wave_pdb_close(handle);
  }

  ILOG4_D("<< exit code - %d", result);
  return result;
}

void __MTLK_IFUNC
wave_pdb_get_mac(mtlk_pdb_t* obj, mtlk_pdb_id_t id, void * mac)
{
  mtlk_pdb_handle_t handle;

  ILOG4_V(">>");

  handle = wave_pdb_open(obj, id);
  mtlk_pdb_fast_get_mac(handle, mac);
  wave_pdb_close(handle);

  ILOG3_DY("<< get MAC param: id(%u) MAC(%Y) ", id, mac);
}

unsigned __MTLK_IFUNC
mtlk_pdb_cmp_mac (mtlk_pdb_t* obj, mtlk_pdb_id_t id, void * mac)
{
  mtlk_pdb_handle_t handle;
  unsigned res;

  ILOG4_V(">>");

  handle = wave_pdb_open(obj, id);
  res = mtlk_pdb_fast_cmp_mac(handle, mac);
  wave_pdb_close(handle);

  ILOG3_DY("<< cmp MAC param: id(%u) MAC(%Y) ", id, mac);
  return res;
}

void __MTLK_IFUNC
wave_pdb_set_int(mtlk_pdb_t* obj, mtlk_pdb_id_t id, uint32 value) {

  mtlk_pdb_handle_t handle;

  ILOG4_V(">>");

  handle = wave_pdb_open(obj, id);
  mtlk_pdb_fast_set_int(handle, value);
  wave_pdb_close(handle);

  ILOG1_DD("<< set param: id(%u) value(%d)", id, value);
}

int __MTLK_IFUNC
wave_pdb_set_string(mtlk_pdb_t* obj, mtlk_pdb_id_t id, const char * value)
{
  int result = MTLK_ERR_OK;
  mtlk_pdb_handle_t handle;

  ILOG4_V(">>");

  handle = wave_pdb_open(obj, id);
  result = mtlk_pdb_fast_set_string(handle, value);
  wave_pdb_close(handle);

  ILOG1_DDS("<< set string param: res(%d) id(%u) val(%s)", result, id, value);
  return result;
}

int __MTLK_IFUNC
wave_pdb_set_binary(mtlk_pdb_t* obj, mtlk_pdb_id_t id, const void * buffer, mtlk_pdb_size_t size) {
  int result = MTLK_ERR_OK;
  mtlk_pdb_handle_t handle;

  ILOG4_V(">>");

  handle = wave_pdb_open(obj, id);
  if(handle) {
    result = mtlk_pdb_fast_set_binary(handle, buffer, size);
    wave_pdb_close(handle);
  }

  ILOG1_DD("<< set binary param: res(%d) id(%u) ", result, id);

  return result;
}

void __MTLK_IFUNC
wave_pdb_set_mac(mtlk_pdb_t* obj, mtlk_pdb_id_t id, const void *mac)
{
  mtlk_pdb_handle_t handle;

  ILOG4_V(">>");

  handle = wave_pdb_open(obj, id);
  mtlk_pdb_fast_set_mac(handle, mac);
  wave_pdb_close(handle);

  ILOG1_DY("<< set MAC param: id(%u) MAC(%Y) ", id, mac);
}

#ifndef MTLK_PDB_UNIT_TEST

int __MTLK_IFUNC
wave_pdb_unit_test(mtlk_pdb_t* obj) {
  return MTLK_ERR_OK;
}

#else

int __MTLK_IFUNC
wave_pdb_unit_test(mtlk_pdb_t* obj) {
  int value;
  char buffer[256];
  int test_size = 256;
  int return_code;

  /* testing wave_pdb_get_int */
  return_code = wave_pdb_get_int(obj, PARAM_DB_MODULE_A_TEST_INT1, &value);
  ILOG0_DDD("ID %d, value %d, ret_code = %d", PARAM_DB_MODULE_A_TEST_INT1, value, return_code);

  return_code = wave_pdb_get_int(obj, PARAM_DB_MODULE_A_TEST_INT2, &value);
  ILOG0_DDD("ID %d, value %d, ret_code = %d", PARAM_DB_MODULE_A_TEST_INT1, value, return_code);

  return_code = wave_pdb_get_int(obj, PARAM_DB_MODULE_B_TEST_INT1, &value);
  ILOG0_DDD("ID %d, value %d, ret_code = %d", PARAM_DB_MODULE_A_TEST_INT1, value, return_code);



  /* error cases - should return error codes or cause assertions*/
  /* Tested
  return_code = wave_pdb_get_int(obj, PARAM_DB_LAST_VALUE_ID + 666, &value);
  ILOG0_DDD("ID %d, value %d, ret_code = %d", PARAM_DB_LAST_VALUE_ID + 666, value, return_code);

  return_code = wave_pdb_get_int(NULL, PARAM_DB_MODULE_A_TEST_INT1, &value);
  ILOG0_DDD("ID %d, value %d, ret_code = %d", PARAM_DB_MODULE_A_TEST_INT1, value, return_code);

  return_code = wave_pdb_get_int(obj, PARAM_DB_MODULE_A_TEST_INT1, NULL);
  ILOG0_DDD("ID %d, value %d, ret_code = %d", PARAM_DB_MODULE_A_TEST_INT1, value, return_code);

  return_code = wave_pdb_get_int(obj, PARAM_DB_MODULE_A_TEST_BINARY, &value);
  ILOG0_DDD("ID %d, value %d, ret_code = %d", PARAM_DB_MODULE_A_TEST_INT1, value, return_code);

  return_code = wave_pdb_get_int(obj, PARAM_DB_MODULE_A_TEST_STRING, &value);
  ILOG0_DDD("ID %d, value %d, ret_code = %d", PARAM_DB_MODULE_A_TEST_INT1, value, return_code);
  */
  /* end testing wave_pdb_get_int */


  /* testing wave_pdb_get_string */
  return_code = wave_pdb_get_string(obj, PARAM_DB_MODULE_A_TEST_STRING, buffer, &test_size);
  ILOG0_DDSD("ID %d, size %d, value %s, ret_code = %d", PARAM_DB_MODULE_A_TEST_STRING, test_size, buffer, return_code);

  return_code = wave_pdb_get_string(obj, PARAM_DB_MODULE_B_TEST_STRING, buffer, &test_size);
  ILOG0_DDSD("ID %d, size %d, value %s, ret_code = %d", PARAM_DB_MODULE_B_TEST_STRING, test_size, buffer, return_code);

  test_size = 4;
  return_code = wave_pdb_get_string(obj, PARAM_DB_MODULE_B_TEST_STRING, buffer, &test_size);
  ILOG0_DDSD("ID %d, size %d, value %s, ret_code = %d", PARAM_DB_MODULE_B_TEST_STRING, test_size, buffer, return_code);

  test_size = 256;
  return_code = wave_pdb_get_string(obj, PARAM_DB_MODULE_B_TEST_STRING, buffer, &test_size);
  ILOG0_DDSD("ID %d, size %d, value %s, ret_code = %d", PARAM_DB_MODULE_B_TEST_STRING, test_size, buffer, return_code);

      /* error cases - should return error codes or cause assertions*/
  /* end testing wave_pdb_get_string */


  /* testing wave_pdb_get_binary */
  test_size = 256;
  return_code = wave_pdb_get_binary(obj, PARAM_DB_MODULE_A_TEST_BINARY, buffer, &test_size);
  ILOG0_DDD("ID %d, size %d, ret_code = %d", PARAM_DB_MODULE_A_TEST_BINARY, test_size, return_code);
  for(value = 0; value < test_size; value++) {
      ILOG0_DD("binary[%d] = %d", value, buffer[value]);
  }
      /* error cases - should return error codes or cause assertions*/
  /* end testing wave_pdb_get_binary */

  /* testing wave_pdb_set_int */
  return_code = wave_pdb_set_int(obj, PARAM_DB_MODULE_A_TEST_INT1, 1234);
  ILOG0_DD("ID %d, ret_code after set = %d", PARAM_DB_MODULE_A_TEST_INT1, return_code);
  return_code = wave_pdb_get_int(obj, PARAM_DB_MODULE_A_TEST_INT1, &value);
  ILOG0_DDD("ID %d, value %d, ret_code = %d", PARAM_DB_MODULE_A_TEST_INT1, value, return_code);

  return_code = wave_pdb_set_int(obj, PARAM_DB_MODULE_A_TEST_INT1, 0x1000);
  ILOG0_DD("ID %d, ret_code after set = %d", PARAM_DB_MODULE_A_TEST_INT1, return_code);
  return_code = wave_pdb_get_int(obj, PARAM_DB_MODULE_A_TEST_INT1, &value);
  ILOG0_DDD("ID %d, value %d, ret_code = %d", PARAM_DB_MODULE_A_TEST_INT1, value, return_code);
      /* error cases - should return error codes or cause assertions*/
  /* end testing wave_pdb_set_int */

  /* testing wave_pdb_set_string */
  return_code = wave_pdb_set_string(obj, PARAM_DB_MODULE_B_TEST_STRING, "Good bye12!");
  ILOG0_DD("ID %d, ret_code after set = %d", PARAM_DB_MODULE_B_TEST_STRING, return_code);
  test_size = 256;
  return_code = wave_pdb_get_string(obj, PARAM_DB_MODULE_B_TEST_STRING, buffer, &test_size);
  ILOG0_DDSD("ID %d, size %d, value %s, ret_code = %d", PARAM_DB_MODULE_B_TEST_STRING, test_size, buffer, return_code);

  return_code = wave_pdb_set_string(obj, PARAM_DB_MODULE_B_TEST_STRING, "Good 12345!");
  ILOG0_DD("ID %d, ret_code after set = %d", PARAM_DB_MODULE_B_TEST_STRING, return_code);
  test_size = 256;
  return_code = wave_pdb_get_string(obj, PARAM_DB_MODULE_B_TEST_STRING, buffer, &test_size);
  ILOG0_DDSD("ID %d, size %d, value %s, ret_code = %d", PARAM_DB_MODULE_B_TEST_STRING, test_size, buffer, return_code);
      /* error cases - should return error codes or cause assertions*/
  /* end testing wave_pdb_set_string */

  /* testing wave_pdb_set_binary */
  buffer[0] = 4;
  buffer[1] = 2;
  return_code = wave_pdb_set_binary(obj, PARAM_DB_MODULE_A_TEST_BINARY, buffer, 2);
  ILOG0_DD("ID %d, ret_code = %d", PARAM_DB_MODULE_A_TEST_BINARY, return_code);

  test_size = 256;
  return_code = wave_pdb_get_binary(obj, PARAM_DB_MODULE_A_TEST_BINARY, buffer, &test_size);
  ILOG0_DDD("ID %d, size %d, ret_code = %d", PARAM_DB_MODULE_A_TEST_BINARY, test_size, return_code);
  for(value = 0; value < test_size; value++) {
      ILOG0_DD("binary[%d] = %d", value, buffer[value]);
  }

  buffer[0] = 2;
  buffer[1] = 4;
  return_code = wave_pdb_set_binary(obj, PARAM_DB_MODULE_A_TEST_BINARY, buffer, 2);
  ILOG0_DD("ID %d, ret_code = %d", PARAM_DB_MODULE_A_TEST_BINARY, return_code);

  test_size = 256;
  return_code = wave_pdb_get_binary(obj, PARAM_DB_MODULE_A_TEST_BINARY, buffer, &test_size);
  ILOG0_DDD("ID %d, size %d, ret_code = %d", PARAM_DB_MODULE_A_TEST_BINARY, test_size, return_code);
  for(value = 0; value < test_size; value++) {
      ILOG0_DD("binary[%d] = %d", value, buffer[value]);
  }
      /* error cases - should return error codes or cause assertions*/
  /* end testing wave_pdb_set_binary */

  /* Test open ref counters - use rmmod to get assert message*/

  {
    mtlk_pdb_handle_t handle ;
    int result;
    handle = wave_pdb_open(obj, PARAM_DB_MODULE_A_TEST_BINARY);

    if(handle) {
      buffer[0] = 2;
      buffer[1] = 4;

      result = mtlk_pdb_fast_cmp_binary(handle, buffer, 2);
      ILOG0_D("Binary comparison result 1 %d", result);

      result = mtlk_pdb_fast_cmp_binary(handle, buffer, 1);
      ILOG0_D("Binary comparison result 2 %d", result);

      result = mtlk_pdb_fast_cmp_binary(handle, buffer, 122);
      ILOG0_D("Binary comparison result 3 %d", result);


      buffer[0] = 4;
      buffer[1] = 2;

      result = mtlk_pdb_fast_cmp_binary(handle, buffer, 2);
      ILOG0_D("Binary comparison result 4 %d", result);

      wave_pdb_close(handle);
    }
  /*  handle = wave_pdb_open(obj, PARAM_DB_MODULE_A_TEST_BINARY);
    handle = wave_pdb_open(obj, PARAM_DB_MODULE_B_TEST_STRING);*/
  }

  return MTLK_ERR_OK;
}
#endif /* MTLK_PDB_UNIT_TEST */
