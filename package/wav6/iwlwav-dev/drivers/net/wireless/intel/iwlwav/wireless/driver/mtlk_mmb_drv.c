/******************************************************************************

                               Copyright (c) 2012
                            Lantiq Deutschland GmbH

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/
#include "mtlkinc.h"
#include "hw_mmb.h"
#include "drvver.h"
#include "mtlk_fast_mem.h"
#include "mtlk_vap_manager.h"
#include "mtlk_dfg.h"
#include "mtlkhal.h"
#include "cfg80211.h"
#include "mac80211.h"
#include "mtlk_pcoc.h"
#include "wave_radio.h"
#include "mtlk_df.h"
#include "fw_recovery.h"
#include "wave_fapi_if.h"

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/pci.h>

#ifdef CPTCFG_IWLWAV_BENCHMARK_TOOLS
  #include "mtlk_dbg.h"
#endif

#if !defined(__i386) && !defined(__x86_64)
  #include <asm/mach-lantiq/lantiq.h>
#endif

#define LOG_LOCAL_GID   GID_MMBDRV
#define LOG_LOCAL_FID   1

#define MTLK_MEM_BAR0_INDEX  0
#define MTLK_MEM_BAR1_INDEX  1

#ifdef CPTCFG_IWLWAV_BUS_PCI_PCIE
  /* we only support 32-bit addresses */
  #define PCI_SUPPORTED_DMA_MASK       0xffffffff
#endif /* CPTCFG_IWLWAV_BUS_PCI_PCIE */

MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_AUTHOR(MTLK_COPYRIGHT);
MODULE_LICENSE("GPL");

static int ap[MTLK_MAX_HW_ADAPTERS_SUPPORTED]         = {1, 1, 1, 1};
static int dut[MTLK_MAX_HW_ADAPTERS_SUPPORTED]        = {0, 0, 0, 0};
int tx_ring_size[MTLK_MAX_HW_ADAPTERS_SUPPORTED]      = {0, 0, 0, 0};

int fastpath[MTLK_MAX_HW_ADAPTERS_SUPPORTED]          = {FASTPATH_DISABLED, FASTPATH_DISABLED,
                                                         FASTPATH_DISABLED, FASTPATH_DISABLED};

int loggersid[MTLK_MAX_HW_ADAPTERS_SUPPORTED]         = {SID_FOR_LOGGER_RX_DATA, SID_FOR_LOGGER_RX_DATA,
                                                         SID_FOR_LOGGER_RX_DATA, SID_FOR_LOGGER_RX_DATA};

int band_cfg_sys_mode[MTLK_MAX_HW_ADAPTERS_SUPPORTED] = {WAVE_HW_RADIO_BAND_CFG_UNSUPPORTED,
                                                         WAVE_HW_RADIO_BAND_CFG_UNSUPPORTED,
                                                         WAVE_HW_RADIO_BAND_CFG_UNSUPPORTED,
                                                         WAVE_HW_RADIO_BAND_CFG_UNSUPPORTED};
/* Kernel Panic on FW assert Enable flag (default value = disabled) */
int panic_on_fw_error[MTLK_MAX_HW_ADAPTERS_SUPPORTED] = {WAVE_RCVRY_KERNEL_PANIC_DEFAULT, WAVE_RCVRY_KERNEL_PANIC_DEFAULT,
                                            WAVE_RCVRY_KERNEL_PANIC_DEFAULT ,WAVE_RCVRY_KERNEL_PANIC_DEFAULT};
int mgmttx_en[MTLK_MAX_HW_ADAPTERS_SUPPORTED]         = {1, 1, 1, 1};
#ifdef MTLK_DEBUG
extern int step_to_fail;
#endif
static int disable_11d_hint = FALSE;
static int no_more_ifaces = FALSE;  /* A flag used to mark if any card startup failed    */
                                    /* In this case we do not bring up any subsequent    */
                                    /* interfaces to avoid wrong wlanX addressing issues */
#ifdef CPTCFG_IWLWAV_SET_PM_QOS
int cpu_dma_latency = MTLK_HW_PM_QOS_DEFAULT; /* Default value of PM_QOS_CPU_DMA_LATENCY */
#endif

#if 1 /* FIXME: just for 5.4.0 backward compatibility, will be removed soon */
static int ahb_off = TRUE;
module_param(ahb_off, int, 0);
#endif

module_param_array(ap, int, NULL, 0);
module_param_array(dut, int, NULL, 0);
module_param_array(tx_ring_size, int, NULL, 0);
module_param_array(fastpath, int, NULL, 0);
module_param_array(loggersid, int, NULL, 0);
module_param_array(band_cfg_sys_mode, int, NULL, 0);
module_param_array(panic_on_fw_error, int, NULL, 0);
module_param_array(mgmttx_en, int, NULL, 0);

#ifdef MTLK_DEBUG
module_param(step_to_fail, int, 0);
#endif
module_param(disable_11d_hint, int, 0);
#ifdef CPTCFG_IWLWAV_SET_PM_QOS
module_param(cpu_dma_latency, int, 0);
#endif

static BOOL dual_pci_over_all = FALSE;
static int dual_pci[MTLK_MAX_HW_ADAPTERS_SUPPORTED] = { 0 }; /* all disabled */
module_param_array(dual_pci, int, NULL, 0);
MODULE_PARM_DESC(dual_pci, "Control detection of DUAL PCI mode");

MODULE_PARM_DESC(ap, "Make an access point (including station mode functionality)");
MODULE_PARM_DESC(dut, "Start driver in device under test (DUT) mode");
MODULE_PARM_DESC(tx_ring_size, "TX ring buffer size");
MODULE_PARM_DESC(fastpath, "Enable FastPath");
MODULE_PARM_DESC(loggersid, "SID for data logger");
MODULE_PARM_DESC(band_cfg_sys_mode, "Set Band Configuration System Mode");
MODULE_PARM_DESC(panic_on_fw_error, "Kernel Panic on FW assert Enable flag");

#ifdef MTLK_DEBUG
MODULE_PARM_DESC(step_to_fail, "Init step to simulate fail");
#endif
MODULE_PARM_DESC(disable_11d_hint, "Disable 802.11d country hint from beacons");
#ifdef CPTCFG_IWLWAV_SET_PM_QOS
MODULE_PARM_DESC(cpu_dma_latency, "Power save DMA latency");
#endif

struct _mtlk_mmb_drv_t {
  struct pci_dev        *pdev;
  struct device         *dev;
  /* mtlk_vap_manager_t    *vap_manager; */
  struct _mtlk_bcl_t    *bcl;
  mtlk_hw_api_t         *hw_api;
  wv_cfg80211_t         *cfg80211;
  wv_mac80211_t         *mac80211;
  mtlk_ccr_t            ccr;
  int                   irq_in[IRQS_TO_HANDLE];
  int                   irq_out[IRQS_TO_HANDLE];
  mtlk_irq_mode_e       irq_mode;
  int                   nof_irqs;
  struct tasklet_struct irq_tasklet[IRQS_TO_HANDLE];
  void                  *bar0;
  void                  *bar0_physical;
  void                  *bar1;
  void                  *bar1_physical; /*physical address for UMT msg */
  mtlk_irq_handler_data irq_handler_data[IRQS_TO_HANDLE];
  BOOL                  g6_pci_aux;  /* Gen6 specific: auxilary PCIe  */
  BOOL                  g6_dual_pci; /* Gen6 specific: dual PCIe mode */

  MTLK_DECLARE_INIT_STATUS;
};

/*------ BCL mode -----------------------------------------*/

static int bcl = 0;
module_param(bcl, int, 0);
MODULE_PARM_DESC(bcl, "Start driver in BCL only mode");

#define BCL_NDEV_NAME   MTLK_NDEV_NAME  /* "wlan" */

#define _LOG_(fmt,...)  printk(KERN_INFO "BCL:(%s:%4d) " fmt "\n", __FUNCTION__, __LINE__, ##__VA_ARGS__)

typedef struct _mtlk_bcl_user_t {
  struct _mtlk_bcl_t    *bcl;
  void                  *iomem;
  struct net_device     *ndev;
  struct net_device_ops  dev_ops;
  struct wireless_dev    wdev;
} mtlk_bcl_user_t;

typedef struct _mtlk_bcl_t {
  struct device         *dev;
  mtlk_bcl_user_t       *bcl_user;
  struct wiphy          *wiphy;
  wave_uint              card_idx;
} mtlk_bcl_t;

static int g_mtlk_bcl_card_idx = 0;

BOOL __MTLK_IFUNC
mtlk_mmb_is_bcl (void)
{
  return (0 != bcl);
}

/**
  FIXCFG80211: description
*/

static struct cfg80211_ops mtlk_bcl_wv_ops = {
  .add_virtual_intf = NULL,
  .del_virtual_intf = NULL,
  .start_ap         = NULL,
  .stop_ap          = NULL,
};

static const struct ieee80211_txrx_stypes
wv_mgmt_stypes[NUM_NL80211_IFTYPES] = {
  [NL80211_IFTYPE_AP] = {
    .tx = 0xFFFF, /* WV_STYPE_ALL,  */
    .rx = 0xFFFF, /* WV_STYPE_AP_RX */
  },
};

static void * _wiphy_privid = &_wiphy_privid;

static struct wiphy *
_mtlk_bcl_wiphy_create (void)
{
  struct wiphy *wiphy;

  /* allocate Linux Wireless Hardware with cfg80211 in private section */
  wiphy = wiphy_new(&mtlk_bcl_wv_ops, 0); /* without private data */
  _LOG_("created wiphy %p", wiphy);
  if (!wiphy) {
    ELOG_V("Couldn't allocate WIPHY device");
    return NULL;
  }

  return wiphy;
}

static void
_mtlk_bcl_wiphy_delete (struct wiphy *wiphy)
{
  _LOG_("delete wiphy %p", wiphy);
  if (wiphy) {
#if 0 /* FIXME: TBD if needed */
    wiphy_unregister(wiphy);
#endif
    wiphy_free(wiphy);
  }
}

static int
_mtlk_bcl_wiphy_init (struct wiphy *wiphy, struct device *dev)
{
  int res = MTLK_ERR_OK;

  /* initialize WIPHY */
  wiphy->mgmt_stypes = wv_mgmt_stypes;

  /* set device pointer for WIPHY */
  set_wiphy_dev(wiphy, dev);

  wiphy->interface_modes = BIT(NL80211_IFTYPE_AP);

  wiphy->bands[NL80211_BAND_2GHZ] = NULL;
  wiphy->bands[NL80211_BAND_5GHZ] = NULL;

  wiphy->privid = _wiphy_privid;

  return res;
}

/*-------------------------------------------*/
/* IW private IOCTL handlers and table
*/

#include "mtlk_df_user_priv.h"

static int
_mtlk_bcl_ui_linux_ioctl_getname (struct net_device *ndev,
                          struct iw_request_info *info,
                          union iwreq_data *wrqu, char *extra)
{
  char *s_net_mode = "802.11abgn";  /* any */

  _LOG_("name_ptr %p, mode %s", wrqu->name, s_net_mode);

  wave_strcopy(wrqu->name, s_net_mode, sizeof(wrqu->name));

  return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_OK);
}

/* FIXCFG80211: Implement all commented functions to support IW */
static const iw_handler  mtlk_bcl_standard_handlers[] = {
  [IW_IOCTL_IDX(SIOCSIWCOMMIT)]     = NULL,
  /* use own handler instead of the provided by wext-compat.c */
  [IW_IOCTL_IDX(SIOCGIWNAME)]  = (iw_handler) _mtlk_bcl_ui_linux_ioctl_getname, /* used by BclSockServer */
};

/* FIXME: limit IO offset to PCI IO region 16 MB */
#define __PCI_IO_OFFS_MASK__    ((1u << 24) - 1) /* 16M - accessible from the host */

static uint32
_mtlk_bcl_io_offs_check (uint32 offs)
{
  uint32 res = (offs & __PCI_IO_OFFS_MASK__);
  if (res != offs) {
    /* _LOG_("BclReq IO offset 0x%08X is limited to 0x%08X", offs, res); */
  }
  return res;
}

static uint32
_mtlk_bcl_io_size_check (uint32 size, uint32 max)
{
  uint32 res = size;
  if (res > max) {
      res = max;
    _LOG_("BclReq size %d is limited to %d", size, res);
  }
  return res;
}

static void *
_mtlk_bcl_ui_ndev_iomem_get (struct net_device *ndev)
{
  mtlk_bcl_user_t *bcl_user = (mtlk_bcl_user_t*) netdev_priv(ndev);

  MTLK_ASSERT(bcl_user);

  return bcl_user->iomem;
}

/* FIXME: BBStudio expect all data in Little Endian */

static __INLINE void
_mtlk_bcl_copy_words_fromio (uint32 *to, const uint32 *from, uint32 count)
{
  while (count--) {
    *to++ = CPU_TO_LE32(mtlk_raw_readl((void *)from++));
  }
}

static __INLINE void
_mtlk_bcl_copy_words_toio (uint32 *to, const uint32 *from, uint32 count)
{
  while (count--) {
    mtlk_raw_writel(LE32_TO_CPU(*from++), to++);
  }
}

static int
_mtlk_bcl_ui_iw_mac_data_proc (struct net_device *ndev, struct iw_request_info *info,
                              union iwreq_data *wrqu, char *extra, int req_get)
{
  void              *iomem;
  int                res = MTLK_ERR_OK;
  uint32             offs, count;
  UMI_BCL_REQUEST    req;

#if 0 /* DEBUG */
  mtlk_bcl_user_t   *bcl_user = (mtlk_bcl_user_t*) netdev_priv(ndev);
  _LOG_("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  _LOG_("bcl_user %p, iomem %p", bcl_user, bcl_user->iomem);
#endif

  if (0 != (copy_from_user(&req, wrqu->data.pointer, sizeof(req)))) {
    res = MTLK_ERR_PARAMS;
    goto finish;
  }

  iomem = _mtlk_bcl_ui_ndev_iomem_get(ndev);

  ILOG1_SDDDP("BCL_REQ_%s: Unit %d, Addr %04X, size %d, iomem %p",
        req_get ? "GET" : "SET", req.Unit, req.Address, req.Size, iomem);

  offs  = _mtlk_bcl_io_offs_check(req.Address);
  count = _mtlk_bcl_io_size_check(req.Size, ARRAY_SIZE(req.Data));

  /* get or set data */
  if (req_get) { /* get */
    _mtlk_bcl_copy_words_fromio(req.Data, (iomem + offs), count);
  } else { /* set */
    _mtlk_bcl_copy_words_toio((iomem + offs), req.Data, count);
  }

  if (0 != (copy_to_user(wrqu->data.pointer, &req, sizeof(req)))) {
    res = MTLK_ERR_UNKNOWN;
  }

finish:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

static int
_mtlk_bcl_ui_iw_mac_data_get (struct net_device *ndev, struct iw_request_info *info,
                              union iwreq_data *wrqu, char *extra)
{
  return _mtlk_bcl_ui_iw_mac_data_proc(ndev, info, wrqu, extra, 1);
}

static int
_mtlk_bcl_ui_iw_mac_data_set (struct net_device *ndev, struct iw_request_info *info,
                              union iwreq_data *wrqu, char *extra)
{
  return _mtlk_bcl_ui_iw_mac_data_proc(ndev, info, wrqu, extra, 0);
}

static const iw_handler  mtlk_bcl_private_handlers[] = {
  [20] = _mtlk_bcl_ui_iw_mac_data_get,
  [21] = _mtlk_bcl_ui_iw_mac_data_set,
  [31] = NULL,
};

const struct iw_handler_def  mtlk_bcl_handler_def = {
  .num_standard     = ARRAY_SIZE(mtlk_bcl_standard_handlers),
  .num_private      = ARRAY_SIZE(mtlk_bcl_private_handlers),
  .private          = (iw_handler *) mtlk_bcl_private_handlers,
  .standard         = (iw_handler *) mtlk_bcl_standard_handlers,
};

/*-------------------------------------------*/
/* ndo_ops */

static int
_mtlk_bcl_ndev_open (struct net_device *ndev)
{
  mtlk_bcl_user_t   *bcl_user = (mtlk_bcl_user_t*) netdev_priv(ndev);

  _LOG_("%s: open, private %p", ndev->name, bcl_user);
  return 0;
}

static int
_mtlk_bcl_ndev_stop (struct net_device *ndev)
{
  mtlk_bcl_user_t   *bcl_user = (mtlk_bcl_user_t*) netdev_priv(ndev);

  _LOG_("%s: stop, private %p", ndev->name, bcl_user);
  return 0;
}

static void
_mtlk_bcl_user_fill_callbacks (mtlk_bcl_user_t *bcl_user)
{
  /* configure ops */
  bcl_user->dev_ops.ndo_open            = _mtlk_bcl_ndev_open; /* up */
  bcl_user->dev_ops.ndo_stop            = _mtlk_bcl_ndev_stop; /* down */

#if 0 /* FIXME: is needed ??? */
  bcl_user->dev_ops.ndo_do_ioctl        = _mtlk_bcl_do_command;
#endif

  bcl_user->ndev->netdev_ops            = &bcl_user->dev_ops;
}

static void
_mtlk_bcl_user_netdev_init (mtlk_bcl_user_t *bcl_user, struct wiphy *wiphy)
{
  /* link network device with wireless device */
  bcl_user->ndev->ieee80211_ptr = &bcl_user->wdev;

  /* initialize wireless device */
  bcl_user->wdev.wiphy = wiphy;

  /* configure interface type */
  bcl_user->wdev.iftype = NL80211_IFTYPE_AP;

  /* link network device with parent device of wireless hardware */
  SET_NETDEV_DEV(bcl_user->ndev, wiphy_dev(wiphy));

  _mtlk_bcl_user_fill_callbacks(bcl_user);

  /* setup destructor */
  /* TODO IWLWAV: FIX
  bcl_user->ndev->destructor = free_netdev;
  */

  /* configure handlers */
  bcl_user->ndev->wireless_handlers = (struct iw_handler_def *)&mtlk_bcl_handler_def;
}

static void
_mmb_drv_bcl_user_delete (mtlk_bcl_user_t *bcl_user)
{
  struct net_device *ndev;

  _LOG_("bcl_user %p", bcl_user);
  if (NULL == bcl_user) return;

  ndev = bcl_user->ndev;
  if(NULL != ndev) {
    /* TODO IWLWAV: FIX
    _LOG_("ndev (%p), destructor (%p)", ndev, ndev->destructor);
#if 1
      unregister_netdev(ndev);
      _LOG_("unregister_netdev Ok");

      if (ndev && !ndev->destructor) {
        free_netdev(ndev);
      }
#endif
    */
  }
}

static void
_mmb_drv_bcl_drv_cleanup (mtlk_mmb_drv_t *obj)
{

  MTLK_ASSERT(obj);

  _LOG_("obj %p, bcl %p", obj, obj->bcl);

  if (NULL != obj->bcl) {
    _mmb_drv_bcl_user_delete(obj->bcl->bcl_user);
    _mtlk_bcl_wiphy_delete(obj->bcl->wiphy);

    mtlk_fast_mem_free(obj->bcl);
    obj->bcl = NULL;
  }

  _LOG_("BCL driver unloaded");
}

static mtlk_bcl_user_t *
_mmb_drv_bcl_user_create (mtlk_bcl_t *bcl, void *iomem)
{
  struct net_device *ndev;
  mtlk_bcl_user_t   *bcl_user = NULL;
  size_t             size = sizeof(mtlk_bcl_user_t);
  int                res;

  ndev  = alloc_netdev(size, BCL_NDEV_NAME "%d", NET_NAME_UNKNOWN, ether_setup);

  if (NULL == ndev) {
    _LOG_("Failed to alloc NDEV");
    return NULL;
  }

  bcl_user = netdev_priv(ndev);
 _LOG_("Allocated NDEV %p of %zu bytes size, private %p", ndev, size, bcl_user);

  /* Init */
  bcl_user->ndev  = ndev;
  bcl_user->bcl   = bcl;
  bcl_user->iomem = iomem;

#if 1
  _mtlk_bcl_user_netdev_init(bcl_user, bcl->wiphy);

  res = register_netdev(ndev);
  if (0 != res) {
    _LOG_("register_netdev failre (%d)", res);
    free_netdev(ndev);
    return NULL;
  }
#endif

  return bcl_user;
}

static int
_mmb_drv_bcl_drv_init (mtlk_mmb_drv_t *obj, mtlk_card_type_t hw_type)
{
  mtlk_bcl_t        *bcl;
  size_t             size;
  int                res;

  _LOG_("obj %p, hw_type %04X", obj, hw_type);

  size = sizeof(*bcl);
  bcl = mtlk_fast_mem_alloc(MTLK_FM_USER_MMBDRV, size);
  obj->bcl = bcl;
  if(NULL == bcl) {
    _LOG_("Failed alloc BCL");
    return MTLK_ERR_NO_MEM;
  }

  bcl->card_idx = g_mtlk_bcl_card_idx++;
  bcl->dev   = obj->dev;
  _LOG_("Allocated BCL %p of %zu bytes size for card_idx %zu (dev %p)", bcl, size, bcl->card_idx, bcl->dev);

  bcl->wiphy = _mtlk_bcl_wiphy_create();
  if (!bcl->wiphy) {
    return MTLK_ERR_NO_MEM;
  }

  res = _mtlk_bcl_wiphy_init(bcl->wiphy, obj->dev);
  if (MTLK_ERR_OK != res) {
      return res;
  }

  bcl->bcl_user = _mmb_drv_bcl_user_create(bcl, obj->bar1 /* the same as ccr_mem->pas */);
  if (NULL == bcl->bcl_user) {
      res = MTLK_ERR_NO_MEM;
  }

  return res;
}

/**************************************************************
 *  MMB driver external interface                             *
 **************************************************************/

/**************************************************************
 *  Basic device initialization initialization                *
 **************************************************************/

#ifdef CPTCFG_IWLWAV_BUS_PCI_PCIE
/**
 * Map physical memory of PCI bus
 */
static void
_pci_mem_get (struct pci_dev *dev, int res_id, void **mapped, void **physical)
{
  int   len;
  resource_size_t resource_start;

  MTLK_ASSERT(NULL != dev);
  MTLK_ASSERT(NULL != mapped);
  MTLK_ASSERT(NULL != physical);

  len = pci_resource_len(dev, res_id);
  resource_start = pci_resource_start(dev, res_id);
  *mapped   = (void *)ioremap(resource_start, len);
  *physical = (void *)(uintptr_t)(resource_start);

  ILOG0_DPPD("PCI Memory block %i: PA: 0x%p, VA: 0x%p, Len=0x%x", res_id,
            *physical, *mapped, len);
}

/**
 * Release physical memory of pci bus
 */
static __INLINE void
_pci_mem_put (void* mem)
{
  if (NULL != mem)
    iounmap(mem);
}

/**
 * Common PCI device cleanup
 */
static __INLINE void
_pci_stop (struct pci_dev *pdev, mtlk_mmb_drv_t *obj)
{
  if (FALSE == obj->g6_pci_aux) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,8,0)
    pci_disable_msi(pdev);
#else
    pci_free_irq_vectors(pdev); /* TODO IWLWAV: Maybe should be done always and not only in the if? */
#endif
  }

  /* remove link for resources */
  pci_set_drvdata(pdev, NULL);

  /* release device memory */
  _pci_mem_put(obj->bar1);

  /* disable device */
  pci_disable_device(pdev);

}


static void
_pci_detect_dual_pci_mode (mtlk_mmb_drv_t *obj, uint32 chip_id)
{
  if (_chipid_is_gen6(chip_id)) {
    obj->g6_dual_pci = wave_pcieg6_is_mode_dual_pci(obj->bar1);
    mtlk_osal_emergency_print("DUAL PCI mode %s detected", obj->g6_dual_pci ? "is" : "not");
  }
}

/**
 * Common PCI device initialization
 */
static __INLINE int
_pci_start (struct pci_dev *pdev, mtlk_mmb_drv_t *obj, mtlk_card_type_t hw_type,
            unsigned device_id, wave_uint card_idx)
{
  BOOL      dual_pci_cfg = FALSE;
  int       res = 0;

  dual_pci_cfg = !!dual_pci[card_idx];

  ILOG1_DDD("CID-%02x: dual_pci insmod param %d, over all %d",
           card_idx, (int)dual_pci_cfg, (int)dual_pci_over_all);

  /* enable PCI device */
  res = pci_enable_device(pdev);
  if (0 != res) {
    return res;
  }

  /* set master mode for PCI device */
  pci_set_master(pdev);

  /* request device memory for BAR1 */
  _pci_mem_get(pdev, MTLK_MEM_BAR0_INDEX, &obj->bar1, &obj->bar1_physical);
  if (NULL == obj->bar1) {
    return -ENOMEM;
  }

  /* PCI early detection for the "main" and "aux" and dual PCI mode */
  obj->g6_pci_aux = FALSE;
  obj->g6_dual_pci = FALSE;

  if (MTLK_CARD_PCIEG6 == hw_type) {
    uint32 chip_id = wave_pcieg6_chip_id_read(obj->bar1);

    obj->g6_pci_aux = _chipid_is_gen6_aux(chip_id);
#if 1 /* FIXME: only one AUX is supported */
    if (obj->g6_pci_aux && (!dual_pci_over_all)) {
#else
    if (obj->g6_pci_aux && (!dual_pci_cfg)) {
#endif
        WLOG_D("CID-%02x: Don't start PCI AUX device due to non DUAL PCI mode", card_idx);
        return -ENODEV;
    };
    if (!obj->g6_pci_aux && (dual_pci_cfg)) {
      WLOG_D("CID-%02x: Detecting DUAL PCI mode...", card_idx);
      _pci_detect_dual_pci_mode(obj, chip_id);
    }
#ifdef CPTCFG_IWLWAV_X86_PUMA7_WAV600_NOSNOOP
    /* Will operate in the no snoop mode. Set PCIe capability accordingly. */
    pcie_capability_clear_and_set_word(pdev, PCI_EXP_DEVCTL, 0, PCI_EXP_DEVCTL_NOSNOOP_EN);
    ILOG0_V("PCIe capability set to PCI_EXP_DEVCTL_NOSNOOP_EN");
#endif
  } else { /* non Wave600 device */
    WLOG_DDD("CID-%02x: Skip detection of DUAL PCI for device_id 0x%04X, dual_pci %d",
           card_idx, device_id, dual_pci_cfg);
  }

  if (FALSE == obj->g6_pci_aux) {
    /* configure DMA */
    res = pci_set_dma_mask(pdev, PCI_SUPPORTED_DMA_MASK);
    if (0 != res) {
      return res;
    }
  }

  if (MTLK_ERR_OK != mtlk_osal_mem_dma_check_availability(&pdev->dev)) {
    ELOG_D("CID-%02x: 32-bit DMA is not available", card_idx);
    return -ENOMEM;
  }

  /* Link PCI device with MMB Drv */
  pci_set_drvdata(pdev, obj);

  /* Link MMB Drv with Linux device */
  obj->pdev = pdev;
  obj->dev = &pdev->dev;

  return res;
}
#endif /*CPTCFG_IWLWAV_BUS_PCI_PCIE*/

#if defined (CPTCFG_IWLWAV_LINDRV_HW_PCIEG5) || defined (CPTCFG_IWLWAV_LINDRV_HW_PCIEG6)

#if defined(CPTCFG_IWLWAV_G6_FPGA_IRAM_TEST)
void hw_mmb_iram_rw_test(mtlk_hw_t *hw);
#endif

static int
pci_irq_alloc(mtlk_mmb_drv_t *obj)
{
  struct pci_dev *pdev = obj->pdev;
  uint32 hw_type = mtlk_hw_eeprom_get_nic_type(obj->hw_api->hw);
  int card_idx = mtlk_hw_mmb_get_card_idx(obj->hw_api->hw);
#ifdef CPTCFG_IWLWAV_USE_INTERRUPT_POLLING
      msi_int  = FALSE; /* Legacy */
      obj->nof_irqs = 1;
      ILOG0_D("CID-%02x: Interrupt configuration: polling mode.", card_idx);
#else
  int msi_nvect;           /* Allocate a vector of MSI interrupts of this length (if possible) */
  int can_use_single_msi;  /* Whether the card support a single MSI mode */
  int can_use_legacy;      /* Whether the card support legacy interrupt mode */
  int res;
  int i;

  if (hw_type == HW_TYPE_HAPS70_G5) {
    msi_nvect          = 4;
    can_use_single_msi = 0;
    can_use_legacy     = 1;
  } else if (hw_type == HW_TYPE_HAPS70_G6) {
    msi_nvect          = 4;
    can_use_single_msi = 1;
    can_use_legacy     = 0;
#ifdef CPTCFG_IWLWAV_LINDRV_HW_PCIEG5
  } else if (MTLK_CARD_PCIEG5 == obj->ccr.hw_type) { /* Wave500 type PCIe cards */
    msi_nvect          = 6;
    can_use_single_msi = 0;
    can_use_legacy     = 1;
#endif
#ifdef CPTCFG_IWLWAV_LINDRV_HW_PCIEG6
  } else if (MTLK_CARD_PCIEG6 == obj->ccr.hw_type) { /* Wave600 type PCIe cards */
#ifdef CPTCFG_IWLWAV_LINDRV_GEN6_USE_LEGACY_INT
    msi_nvect          = 0;
    can_use_single_msi = 0;
    can_use_legacy     = 1;
#else
    msi_nvect          = 6;
    can_use_single_msi = 1;
    can_use_legacy     = 0;
#endif
#endif
  } else { /* Illegal, will produce an error */
    msi_nvect          = 0;
    can_use_single_msi = 0;
    can_use_legacy     = 0;
  }

  obj->irq_mode = MTLK_IRQ_MODE_INVALID;

  /* The first priority is to try an MSI vector */
  if (msi_nvect > 0) {
#ifdef CONFIG_INTEL_PCI_MULTI_MSI
    /* 32-bit of Linux has a propritary support for MSI vector */
    pci_enable_multi_msi_support(pdev);
#endif
/* TODO IWLWAV: Should fix backport to support pci_alloc_irq_vectors (which it doesn't currently) */
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,15,0)
    res = pci_enable_msi_block(pdev, msi_nvect);
    if (res == 0) {
#else
  #if !defined(CPTCFG_IWLWAV_USE_GENL)
    res = pci_enable_msi_range(pdev, msi_nvect, msi_nvect);
  #else
    res = pci_alloc_irq_vectors(pdev, msi_nvect, msi_nvect, PCI_IRQ_MSI);
  #endif
    if (res == msi_nvect) {
#endif
      obj->irq_mode = (msi_nvect > 4) ? MTLK_IRQ_MODE_MSI_8 : MTLK_IRQ_MODE_MSI_4;
      obj->nof_irqs = msi_nvect;
      ILOG0_DD("CID-%02x: Interrupt configuration: using MSI vector of %d interrupts", card_idx, msi_nvect);
    } else {
      ILOG0_D("CID-%02x: Interrupt configuration: MSI vector isn't supported by OS", card_idx);
    }
  }

  /* The second priority is to try a single MSI interrupt */
  if (MTLK_IRQ_MODE_INVALID == obj->irq_mode && can_use_single_msi)
  {
    res = pci_enable_msi(pdev);
    if (res == 0) {
      obj->irq_mode = MTLK_IRQ_MODE_MSI_1;
      obj->nof_irqs = 1;
      ILOG0_D("CID-%02x: Interrupt configuration: using a single MSI interrupt", card_idx);
    } else {
      ILOG0_D("CID-%02x: Interrupt configuration: single MSI interrupt isn't supported by OS", card_idx);
    }
  }

  /* Otherwise use legacy interrupts */
  if (MTLK_IRQ_MODE_INVALID == obj->irq_mode && can_use_legacy)
  {
      obj->irq_mode = MTLK_IRQ_MODE_LEGACY;
      obj->nof_irqs = 1;
      ILOG0_D("CID-%02x: Interrupt configuration: using legacy interrupt mode", card_idx);
  }

  /* Has one of the options listed above worked for us? */
  if (MTLK_IRQ_MODE_INVALID == obj->irq_mode) {
      ELOG_D("CID-%02x: Interrupt allocation failed", card_idx);
      return MTLK_ERR_UNKNOWN;
  }

  for (i = 0; i < obj->nof_irqs; i++) {
    obj->irq_out[i] = pdev->irq + i;
    ILOG1_D("MSI got IRQ: %d", obj->irq_out[i]);
  }
#endif /* CPTCFG_IWLWAV_USE_INTERRUPT_POLLING */

#if defined(CPTCFG_IWLWAV_G6_FPGA_IRAM_TEST)
      hw_mmb_iram_rw_test(obj->hw_api->hw);
#endif

      MTLK_UNREFERENCED_PARAM(card_idx);
      return MTLK_ERR_OK;
}

#endif /* CPTCFG_IWLWAV_BUS_PCI_PCIE */

/**************************************************************
 *  MMB driver interrupt handlers                             *
 **************************************************************/

/**
 * Bottom half of MMB driver IRQ handler
 */
static void
_on_irq_tasklet(unsigned long param)
{
  MTLK_ASSERT(param);

  mtlk_hw_mmb_deferred_handler((mtlk_irq_handler_data *)param);
}

static void
_on_data_cfm_tasklet(unsigned long param)
{
  MTLK_ASSERT(param);

  mtlk_hw_mmb_data_cfm_handler((mtlk_irq_handler_data *)param);
}

static void
_on_data_rx_tasklet(unsigned long param)
{
  MTLK_ASSERT(param);

  mtlk_hw_mmb_data_rx_handler((mtlk_irq_handler_data *)param);
}

static void
_on_bss_cfm_tasklet(unsigned long param)
{
  MTLK_ASSERT(param);

  mtlk_hw_mmb_bss_cfm_handler((mtlk_irq_handler_data *)param);
}

static void
_on_bss_rx_tasklet(unsigned long param)
{
  MTLK_ASSERT(param);

  mtlk_hw_mmb_bss_rx_handler((mtlk_irq_handler_data *)param);
}

static void
_on_mailbox_irq_tasklet(unsigned long param)
{
  MTLK_ASSERT(param);

  mtlk_hw_mmb_mailbox_handler((mtlk_irq_handler_data *)param);
}

static void
_on_bss_shared_tasklet(unsigned long param)
{
  MTLK_ASSERT(param);

  mtlk_hw_mmb_bss_shared_handler((mtlk_irq_handler_data *)param);
}

static void
_on_legacy_irq_tasklet(unsigned long param)
{
  MTLK_ASSERT(param);

  mtlk_hw_mmb_legacy_irq_handler ((mtlk_irq_handler_data *)param);
}

static void
_on_mailbox_legacy_shared_tasklet(unsigned long param)
{
  MTLK_ASSERT(param);
  mtlk_hw_mmb_mailbox_legacy_shared_handler((mtlk_irq_handler_data *)param);
}


typedef void (tasklet_func)(unsigned long);

tasklet_func *tasklets_8[IRQS_TO_HANDLE] = {
  _on_data_cfm_tasklet,
  _on_data_rx_tasklet,
  _on_bss_cfm_tasklet,
  _on_bss_rx_tasklet,
  _on_mailbox_irq_tasklet,
  _on_legacy_irq_tasklet
};

tasklet_func *tasklets_4[4] = {
  _on_data_cfm_tasklet,
  _on_data_rx_tasklet,
  _on_bss_shared_tasklet,
  _on_mailbox_legacy_shared_tasklet
};

tasklet_func *tasklets_1[1] = {
  _on_irq_tasklet,
};


/**
 * Postpone upper half IRQ handler of MMB driver
 */
int __MTLK_IFUNC
mtlk_mmb_drv_postpone_irq_handler (mtlk_irq_handler_data *ihd)
{
  /* postpone IRQ handler */
  tasklet_schedule(&ihd->irq_tasklets[ihd->tasklet]);

  return MTLK_ERR_OK;
}

uint32 __MTLK_IFUNC
mtlk_mmb_drv_get_disable_11d_hint_param(void)
{
  return disable_11d_hint;
}

#ifdef CPTCFG_IWLWAV_BUS_PCI_PCIE
struct pci_dev * __MTLK_IFUNC
mtlk_mmb_drv_get_pci_dev (struct _mtlk_mmb_drv_t *mmb_drv)
{
  MTLK_ASSERT(NULL != mmb_drv);
  return mmb_drv->pdev;
}
#endif /* CPTCFG_IWLWAV_BUS_PCI_PCIE */

#ifndef CPTCFG_IWLWAV_USE_INTERRUPT_POLLING

/**
 * Entry points for MMB driver interrupt handler
 */
static irqreturn_t _on_interrupt_msi(int irq, void *hw) __MTLK_INT_HANDLER_SECTION;
static irqreturn_t _on_interrupt_msi(int irq, void *hw)
{
  MTLK_UNREFERENCED_PARAM(irq);
  MTLK_ASSERT(hw);

  if(MTLK_ERR_OK == mtlk_hw_mmb_interrupt_handler_msi(hw))
    return IRQ_HANDLED;

  return IRQ_NONE;
}

static irqreturn_t _on_interrupt_legacy(int irq, void *hw) __MTLK_INT_HANDLER_SECTION;
static irqreturn_t _on_interrupt_legacy(int irq, void *hw)
{
  MTLK_UNREFERENCED_PARAM(irq);
  MTLK_ASSERT(hw);

  if(MTLK_ERR_OK == mtlk_hw_mmb_interrupt_handler_legacy(hw))
    return IRQ_HANDLED;

  return IRQ_NONE;
}

const mtlk_irq_handler_data ihd_8[] = {
  [0] = {  /* array element order corresponds to IRQ status bit number */
    .hw      = NULL,             /* hw - to be later initialized at runtime */
    .tasklet = 0,                /* bit 0 == DATA TX OUT */
    .irq_no  = MTLK_IRQ_TXOUT,
    .status  = 0                 /* will be read on interrupt */
  },
  [1] = {
    .tasklet = 1,                /* bit 1 == DATA RX OUT */
    .irq_no  = MTLK_IRQ_RX,
  },
  [2] = {
    .tasklet = 2,                /* bit 2 == BSS TX OUT */
    .irq_no  = MTLK_IRQ_BSS_CFM,
  },
  [3] = {
    .tasklet = 3,                /* bit 3 == BSS RX */
    .irq_no  = MTLK_IRQ_BSS_IND,
  },
  [4] = {
    .tasklet = 4,                /* bit 4 == mailbox */
    .irq_no  = MTLK_IRQ_MAILBOX,
  },
  [5] = {
    .tasklet = 5,                /* bit 5 == legacy */
    .irq_no  = MTLK_IRQ_LEGACY,
  }
};

const mtlk_irq_handler_data ihd_4[] = {
  [0] = {  /* array element order corresponds to IRQ status bit number */
    .hw      = NULL,             /* hw - to be later initialized at runtime */
    .tasklet = 0,                /* bit 0 == DATA TX OUT */
    .irq_no  = MTLK_IRQ_TXOUT,
    .status  = 0                 /* will be read on interrupt */
  },
  [1] = {
    .tasklet = 1,                /* bit 1 == DATA RX OUT */
    .irq_no  = MTLK_IRQ_RX,
  },
  [2] = {
    .tasklet = 2,                /* bit 2 == BSS TX OUT and BSS RX */
    .irq_no  = MTLK_IRQ_BSS_CFM | MTLK_IRQ_BSS_IND,
  },
  [3] = {
    .tasklet = 3,                /* bit 3 == legacy */
    .irq_no  = MTLK_IRQ_LEGACY | MTLK_IRQ_MAILBOX,  /* plus host mailbox ??? */
  }
};

const mtlk_irq_handler_data ihd_1[] = {
  [0] = {                        /* just one element here */
    .tasklet = 0,                /* serve all */
    .irq_no  = MTLK_IRQ_TXOUT | MTLK_IRQ_RX |
               MTLK_IRQ_BSS_IND | MTLK_IRQ_BSS_CFM |
               MTLK_IRQ_LEGACY | MTLK_IRQ_MAILBOX
  }
};

/**
 * Register IRQ handler
 */

#if MTLK_USE_DIRECTCONNECT_DP_API
static BOOL
_mmb_drv_is_irqno_required(mtlk_hw_t *hw, uint32 irq_no)
{
  switch (irq_no) {
    case MTLK_IRQ_TXOUT:
      return (!mtlk_mmb_fastpath_available(hw));
    case MTLK_IRQ_RX:
      return (!mtlk_mmb_fastpath_available(hw) || mtlk_mmb_dcdp_frag_wa_enabled(hw));
    default:
      return TRUE;
  }
}
#else
#define _mmb_drv_is_irqno_required(hw, irq_no) (TRUE)
#endif

static int
_mmb_drv_irq_start(mtlk_mmb_drv_t *obj)
{
  int err = MTLK_ERR_OK;
  int i = 0;

  MTLK_ASSERT(obj);
  MTLK_ASSERT(obj->hw_api);
  MTLK_ASSERT(obj->hw_api->hw);

  switch (obj->nof_irqs) {
    case 6:
            break;

    case 4:
#if !defined(CPTCFG_IWLWAV_LINDRV_HW_PCIEG5) && !defined(CPTCFG_IWLWAV_LINDRV_HW_PCIEG6)
            ELOG_V("This IRQ configuration is supported only on PCIE - should not happen on AHB/FPGA");
            MTLK_ASSERT(0);
            return MTLK_ERR_UNKNOWN;
#endif
            break;

    case 1:
            break;

    default: ELOG_D("Unsupported IRQ configuration - %d IRQs", obj->nof_irqs);
             return MTLK_ERR_UNKNOWN;
  }

  mtlk_hw_mmb_set_msi_intr_mode(obj->hw_api->hw, obj->irq_mode);

  for (i = 0; i < obj->nof_irqs; i++) {
    MTLK_ASSERT(obj->irq_out[i]);

    obj->irq_handler_data[i].hw           = obj->hw_api->hw;
    obj->irq_handler_data[i].irq_tasklets = obj->irq_tasklet;
    obj->irq_handler_data[i].status       = MTLK_IRQ_NONE;

    ILOG2_SDD("%s IRQ 0x%x in 0x%x out", DRV_NAME, obj->irq_in[i], obj->irq_out[i]);

    {
      if (_mmb_drv_is_irqno_required(obj->irq_handler_data[i].hw, obj->irq_handler_data[i].irq_no)) {
        err = obj->irq_mode != MTLK_IRQ_MODE_LEGACY ?
                request_irq(obj->irq_out[i], _on_interrupt_msi,    IRQF_SHARED, DRV_NAME, &obj->irq_handler_data[i]) :
                request_irq(obj->irq_out[i], _on_interrupt_legacy, IRQF_SHARED, DRV_NAME, &obj->irq_handler_data[i]);
        if (err < 0) {
          ELOG_DD("Failed to allocate interrupt %d, error code: %d", obj->irq_out[i], err);
          return MTLK_ERR_UNKNOWN;
        }
      }
    }
  }

  return MTLK_ERR_OK;
}

/**
 * Unregister IRW handler
 */
static void
_mmb_drv_irq_stop(mtlk_mmb_drv_t *obj)
{
  int i;

  MTLK_ASSERT(obj);
  MTLK_ASSERT(obj->hw_api);
  MTLK_ASSERT(obj->hw_api->hw);

  for (i = 0; i < obj->nof_irqs; i++) {
   if (_mmb_drv_is_irqno_required(obj->hw_api->hw, obj->irq_handler_data[i].irq_no)) {
     free_irq(obj->irq_out[i], &obj->irq_handler_data[i]);
   }
  }
}
#endif /* not CPTCFG_IWLWAV_USE_INTERRUPT_POLLING */

/**************************************************************
 *  Main MMB driver initialization                            *
 **************************************************************/

/**
 * MMB driver initialization steps
 */
MTLK_INIT_STEPS_LIST_BEGIN(mmb_drv)
  MTLK_INIT_STEPS_LIST_ENTRY(mmb_drv, MMB_HW_CARD_CREATE)
  MTLK_INIT_STEPS_LIST_ENTRY(mmb_drv, MMB_CCR_INIT)
  MTLK_INIT_STEPS_LIST_ENTRY(mmb_drv, MMB_EEPROM_PSDB_INIT)
  MTLK_INIT_STEPS_LIST_ENTRY(mmb_drv, MMB_HW_RADIO_BAND_INIT)
  MTLK_INIT_STEPS_LIST_ENTRY(mmb_drv, MMB_RCVRY_INIT)
  MTLK_INIT_STEPS_LIST_ENTRY(mmb_drv, MMB_RADIO_NUMBER_GET)
  MTLK_INIT_STEPS_LIST_ENTRY(mmb_drv, MMB_RADIO_CREATE)
  MTLK_INIT_STEPS_LIST_ENTRY(mmb_drv, MMB_HW_CARD_INIT)
#ifdef CPTCFG_IWLWAV_BUS_PCI_PCIE
  MTLK_INIT_STEPS_LIST_ENTRY(mmb_drv, MMB_PCI_IRQ_ALLOC)
#endif
  MTLK_INIT_STEPS_LIST_ENTRY(mmb_drv, MMB_IRQ_TASKLET_INIT)
  MTLK_INIT_STEPS_LIST_ENTRY(mmb_drv, MMB_MAC_RESET)
#ifndef CPTCFG_IWLWAV_USE_INTERRUPT_POLLING
  MTLK_INIT_STEPS_LIST_ENTRY(mmb_drv, MMB_IRQ_START)
#endif /* not CPTCFG_IWLWAV_USE_INTERRUPT_POLLING */
  MTLK_INIT_STEPS_LIST_ENTRY(mmb_drv, MMB_CARD_START)
  MTLK_INIT_STEPS_LIST_ENTRY(mmb_drv, WAVE_RADIO_INIT)
  MTLK_INIT_STEPS_LIST_ENTRY(mmb_drv, MMB_TXMM_INIT)
  MTLK_INIT_STEPS_LIST_ENTRY(mmb_drv, MMB_TXDM_INIT)
  MTLK_INIT_STEPS_LIST_ENTRY(mmb_drv, MMB_CARD_START_FINALIZE)
  MTLK_INIT_STEPS_LIST_ENTRY(mmb_drv, MMB_RADIO_START_FINALIZE)
  MTLK_INIT_STEPS_LIST_ENTRY(mmb_drv, MMB_RADIO_CALIBRATE)
  MTLK_INIT_STEPS_LIST_ENTRY(mmb_drv, MMB_MAC_EVENTS_START)
  MTLK_INIT_STEPS_LIST_ENTRY(mmb_drv, MMB_CORE_PREPARE_STOP)
  MTLK_INIT_STEPS_LIST_ENTRY(mmb_drv, MMB_RADIO_MAC80211_INIT)
  MTLK_INIT_STEPS_LIST_ENTRY(mmb_drv, MMB_RCVRY_INIT_FINALIZE)
MTLK_INIT_INNER_STEPS_BEGIN(mmb_drv)
MTLK_INIT_STEPS_LIST_END(mmb_drv);

/**
 * retrieve working mode for latest added HW card
 */
static __INLINE mtlk_work_mode_e
_work_mode_get(mtlk_hw_t *hw)
{
  return dut[mtlk_hw_mmb_get_card_idx(hw)] ? MTLK_MODE_DUT : MTLK_MODE_NON_DUT;
}

/**
 * Init TXMM object
 */
static int
_mmb_txmm_init(mtlk_hw_api_t *hw_api) {
  MTLK_ASSERT(NULL != hw_api);
  MTLK_ASSERT(NULL != hw_api->hw);
  return mtlk_mmb_txmm_init(hw_api->hw, hw_api);
}

/**
 * Init TXDM object
 */
static int
_mmb_txdm_init(mtlk_hw_api_t *hw_api) {
  MTLK_ASSERT(NULL != hw_api);
  MTLK_ASSERT(NULL != hw_api->hw);
  return mtlk_mmb_txdm_init(hw_api->hw, hw_api);
}

void kill_all_tasklets (mtlk_mmb_drv_t *obj)
{
  int i;

  MTLK_ASSERT(obj);
  MTLK_ASSERT(obj->hw_api);
  MTLK_ASSERT(obj->hw_api->hw);

  for (i = 0; i < obj->nof_irqs; i++) {
    if (_mmb_drv_is_irqno_required(obj->hw_api->hw, obj->irq_handler_data[i].irq_no)) {
      tasklet_kill(&obj->irq_tasklet[i]);
    }
  }
}

static BOOL
_hw_state_is_fw_error(mtlk_hw_state_e ohw_state){
  return (ohw_state & (MTLK_HW_STATE_ERROR | MTLK_HW_STATE_EXCEPTION |
          MTLK_HW_STATE_APPFATAL | MTLK_HW_STATE_MAC_ASSERTED));
}

static void
_mmb_fw_dump(mtlk_mmb_drv_t *obj, BOOL is_rollback) {
  mtlk_hw_state_e ohw_state;
  if (is_rollback){
    if (obj && obj->hw_api && obj->hw_api->hw){
      wave_hw_mmb_get_prop(obj->hw_api->hw, MTLK_HW_PROP_STATE, &ohw_state, sizeof(ohw_state));
      /* if we got fw error before init finished we need to handle fw dump here,
        * before we reset the fw and hw state, since recovery does not work yet */
      if (_hw_state_is_fw_error(ohw_state)){
        /* handle fw dump if needed */
        if (wave_rcvry_fw_steady_get(obj->bar1))
          wave_rcvry_fwdump_handle(obj->bar1);
        else
          wave_rcvry_no_dump_event(obj->bar1);
      }
    }
  }
}

/**
 * MMB device deletion procedure
 */
static void
_mmb_drv_cleanup(mtlk_mmb_drv_t *obj, BOOL is_rollback)
{
  wave_radio_descr_t *radio_descr;

  if (mtlk_mmb_is_bcl()) {
    _mmb_drv_bcl_drv_cleanup(obj);
    return;
  }

  if (is_rollback) {
    wave_rcvry_pci_probe_error_set(obj->bar1, TRUE);
  }

  radio_descr = wave_card_radio_descr_get(obj->hw_api->hw);

  MTLK_CLEANUP_BEGIN(mmb_drv, MTLK_OBJ_PTR(obj))
    MTLK_CLEANUP_STEP(mmb_drv, MMB_RCVRY_INIT_FINALIZE, MTLK_OBJ_PTR(obj),
                      wave_rcvry_cleanup_finalize, (obj->hw_api->hw));
    MTLK_CLEANUP_STEP(mmb_drv, MMB_RADIO_MAC80211_INIT, MTLK_OBJ_PTR(obj),
                      wave_radio_mac80211_deinit, (radio_descr));
    MTLK_CLEANUP_STEP(mmb_drv, MMB_CORE_PREPARE_STOP, MTLK_OBJ_PTR(obj),
                      wave_radio_prepare_stop, (radio_descr));
    MTLK_CLEANUP_STEP(mmb_drv, MMB_MAC_EVENTS_START, MTLK_OBJ_PTR(obj),
                      mtlk_hw_mmb_stop_mac_events, (obj->hw_api->hw));
    rtnl_lock();
    MTLK_CLEANUP_STEP(mmb_drv, MMB_RADIO_CALIBRATE, MTLK_OBJ_PTR(obj),
                      MTLK_NOACTION, ());
    MTLK_CLEANUP_STEP(mmb_drv, MMB_RADIO_START_FINALIZE, MTLK_OBJ_PTR(obj),
                      wave_radio_deinit_finalize, (radio_descr));
    /* We prefer not to use steps for that function as we want
     * to always try to get dumps if init fails, even if we didn't
     * get to this step in the init process */
    _mmb_fw_dump(obj, is_rollback);
    MTLK_CLEANUP_STEP(mmb_drv, MMB_CARD_START_FINALIZE, MTLK_OBJ_PTR(obj),
                      mtlk_hw_mmb_stop_card_finalize, (obj->hw_api->hw));
    MTLK_CLEANUP_STEP(mmb_drv, MMB_TXDM_INIT, MTLK_OBJ_PTR(obj),
                      mtlk_txmm_cleanup, (mtlk_hw_mmb_get_txdm_base(obj->hw_api->hw)));
    MTLK_CLEANUP_STEP(mmb_drv, MMB_TXMM_INIT, MTLK_OBJ_PTR(obj),
                      mtlk_txmm_cleanup, (mtlk_hw_mmb_get_txmm_base(obj->hw_api->hw)));
    rtnl_unlock();
    MTLK_CLEANUP_STEP(mmb_drv, WAVE_RADIO_INIT, MTLK_OBJ_PTR(obj),
                      wave_radio_deinit, (radio_descr));
    MTLK_CLEANUP_STEP(mmb_drv, MMB_CARD_START, MTLK_OBJ_PTR(obj),
                      mtlk_hw_mmb_stop_card, (obj->hw_api->hw));
#ifndef CPTCFG_IWLWAV_USE_INTERRUPT_POLLING
    MTLK_CLEANUP_STEP(mmb_drv, MMB_IRQ_START, MTLK_OBJ_PTR(obj),
                      _mmb_drv_irq_stop, (obj));
#endif /* not CPTCFG_IWLWAV_USE_INTERRUPT_POLLING */
    MTLK_CLEANUP_STEP(mmb_drv, MMB_MAC_RESET, MTLK_OBJ_PTR(obj),
                      MTLK_NOACTION, ());
    MTLK_CLEANUP_STEP(mmb_drv, MMB_IRQ_TASKLET_INIT, MTLK_OBJ_PTR(obj),
                      kill_all_tasklets, (obj));
#ifdef CPTCFG_IWLWAV_BUS_PCI_PCIE
    MTLK_CLEANUP_STEP(mmb_drv, MMB_PCI_IRQ_ALLOC, MTLK_OBJ_PTR(obj),
                      MTLK_NOACTION, ());
#endif
    /* todo: align with system engineering team weather should be called per
     * card or per radio. If per radio, move to wave_radio.
     */
    MTLK_CLEANUP_STEP(mmb_drv, MMB_HW_CARD_INIT, MTLK_OBJ_PTR(obj),
                      mtlk_hw_mmb_cleanup_card, (obj->hw_api->hw));
    MTLK_CLEANUP_STEP(mmb_drv, MMB_RADIO_CREATE, MTLK_OBJ_PTR(obj),
                      wave_radio_destroy, (radio_descr));
    MTLK_CLEANUP_STEP(mmb_drv, MMB_RADIO_NUMBER_GET, MTLK_OBJ_PTR(obj),
                      MTLK_NOACTION, ());
    MTLK_CLEANUP_STEP(mmb_drv, MMB_RCVRY_INIT, MTLK_OBJ_PTR(obj),
                      MTLK_NOACTION, ()); /* cleaned up at _pci_remove() */
    MTLK_CLEANUP_STEP(mmb_drv, MMB_HW_RADIO_BAND_INIT, MTLK_OBJ_PTR(obj),
                      MTLK_NOACTION, ());
    MTLK_CLEANUP_STEP(mmb_drv, MMB_EEPROM_PSDB_INIT, MTLK_OBJ_PTR(obj),
                      mtlk_eeprom_psdb_parse_stop, (obj->hw_api->hw));
    MTLK_CLEANUP_STEP(mmb_drv, MMB_CCR_INIT, MTLK_OBJ_PTR(obj),
                      mtlk_ccr_cleanup, (&obj->ccr));
    MTLK_CLEANUP_STEP(mmb_drv, MMB_HW_CARD_CREATE, MTLK_OBJ_PTR(obj),
                      mtlk_hw_mmb_remove_card, (obj->hw_api));
  MTLK_CLEANUP_END(mmb_drv, MTLK_OBJ_PTR(obj));
}

int init_all_tasklets (mtlk_mmb_drv_t *obj)
{
  tasklet_func **tasklets;
  int i;
  const mtlk_irq_handler_data *ihd_defaults = NULL;

  MTLK_ASSERT(obj);
  MTLK_ASSERT(obj->hw_api);
  MTLK_ASSERT(obj->hw_api->hw);

  switch (obj->nof_irqs) {
    case 6: tasklets = tasklets_8;
            ihd_defaults = ihd_8;
            break;
    case 4: tasklets = tasklets_4;
            ihd_defaults = ihd_4;
            break;
    case 1: tasklets = tasklets_1;
            ihd_defaults = ihd_1;
            break;
    default: ELOG_D("Unsupported IRQ configuration - %d IRQs", obj->nof_irqs);
             return MTLK_ERR_UNKNOWN;
  }

  for (i = 0; i < obj->nof_irqs; i++) {
    obj->irq_handler_data[i] = ihd_defaults[i];
    if (_mmb_drv_is_irqno_required(obj->hw_api->hw, obj->irq_handler_data[i].irq_no)) {
      tasklet_init(&obj->irq_tasklet[i], tasklets[i], (mtlk_handle_t) &obj->irq_handler_data[i]);
    }
  }

  return MTLK_ERR_OK;
}

#if defined (CPTCFG_IWLWAV_G6_BAR0_TEST)
void hw_mmb_bar0_test(mtlk_hw_t *hw, char *id_str);
#endif

static int
_mmb_drv_radio_number_get (mtlk_ccr_t *ccr, mtlk_card_type_t hw_type, mtlk_hw_t *hw, unsigned *radio_number)
{
  unsigned n_supported, n_configured;
  int res;

  res = wave_card_radio_number_get(hw_type, &n_supported);
  if (MTLK_ERR_OK != res) {
    return res;
  }

  res = wave_hw_cfg_radio_number_get(ccr, hw, &n_configured);
  if (MTLK_ERR_OK != res) {
    return res;
  }

  ILOG0_DDD("card_idx:%d, number of radios supported:%d, configured:%d",
            mtlk_hw_mmb_get_card_idx(hw), n_supported, n_configured);

  if (n_supported < n_configured) {
    ELOG_V("Wrong radio number configuration");
    return MTLK_ERR_PARAMS;
  }

  *radio_number = n_configured;

  return MTLK_ERR_OK;
}

/**
 * MMB device registration procedure
 */
static int
_mmb_drv_init (mtlk_mmb_drv_t *obj, mtlk_card_type_t hw_type)
{
  mtlk_hw_t *hw = NULL;
  wave_radio_descr_t *radio_descr = NULL;
  unsigned radio_number;

  MTLK_ASSERT(_known_card_type(hw_type));

  if (mtlk_mmb_is_bcl()) {
    int res = _mmb_drv_bcl_drv_init(obj, hw_type);
    if (MTLK_ERR_OK != res) {
      _mmb_drv_bcl_drv_cleanup(obj);
    }
    return res;
  }

  MTLK_INIT_TRY(mmb_drv, MTLK_OBJ_PTR(obj))

    MTLK_INIT_STEP_EX(mmb_drv, MMB_HW_CARD_CREATE, MTLK_OBJ_PTR(obj),
                      mtlk_hw_mmb_add_card, (),
                      obj->hw_api, NULL != obj->hw_api,
                      MTLK_ERR_UNKNOWN);

    MTLK_ASSERT(NULL != obj->hw_api->vft->get_msg_to_send);
    MTLK_ASSERT(NULL != obj->hw_api->vft->send_data);
    MTLK_ASSERT(NULL != obj->hw_api->vft->release_msg_to_send);
    MTLK_ASSERT(NULL != obj->hw_api->vft->set_prop);
    MTLK_ASSERT(NULL != obj->hw_api->vft->get_prop);

    /* get short HW handle accessor */
    hw = obj->hw_api->hw;

    MTLK_INIT_STEP(mmb_drv, MMB_CCR_INIT, MTLK_OBJ_PTR(obj),
                   mtlk_ccr_init, (&obj->ccr, hw_type, hw, obj->bar0, obj->bar1, obj->bar1_physical, obj->dev));

    wave_hw_ccr_set(hw, &obj->ccr);

    MTLK_INIT_STEP(mmb_drv, MMB_EEPROM_PSDB_INIT, MTLK_OBJ_PTR(obj),
                   mtlk_eeprom_psdb_read_and_parse, (hw));

    MTLK_INIT_STEP_VOID(mmb_drv, MMB_HW_RADIO_BAND_INIT, MTLK_OBJ_PTR(obj),
                        wave_hw_radio_band_cfg_set, (hw));

    MTLK_INIT_STEP(mmb_drv, MMB_RCVRY_INIT, MTLK_OBJ_PTR(obj),
                   wave_rcvry_card_add, (obj->bar1, hw));

    MTLK_INIT_STEP(mmb_drv, MMB_RADIO_NUMBER_GET, MTLK_OBJ_PTR(obj),
                   _mmb_drv_radio_number_get, (&obj->ccr, hw_type, hw, &radio_number));

    MTLK_INIT_STEP_EX(mmb_drv, MMB_RADIO_CREATE, MTLK_OBJ_PTR(obj),
                      wave_radio_create,
                      (radio_number, obj->hw_api, _work_mode_get(hw)),
                      radio_descr, NULL != radio_descr, MTLK_ERR_UNKNOWN);

    MTLK_INIT_STEP(mmb_drv, MMB_HW_CARD_INIT, MTLK_OBJ_PTR(obj),
                   mtlk_hw_mmb_init_card, (hw, &obj->ccr, obj->bar1, obj->bar1_physical,
                                           radio_descr, fastpath[mtlk_hw_mmb_get_card_idx(hw)],
                                           obj->g6_dual_pci));

#ifdef CPTCFG_IWLWAV_BUS_PCI_PCIE
    MTLK_INIT_STEP(mmb_drv, MMB_PCI_IRQ_ALLOC, MTLK_OBJ_PTR(obj),
                   pci_irq_alloc, (obj));
#endif

    MTLK_INIT_STEP(mmb_drv, MMB_IRQ_TASKLET_INIT, MTLK_OBJ_PTR(obj),
                   init_all_tasklets, (obj));

    MTLK_INIT_STEP(mmb_drv, MMB_MAC_RESET, MTLK_OBJ_PTR(obj),
                   mtlk_hw_mmb_reset_mac, (hw));

#ifndef CPTCFG_IWLWAV_USE_INTERRUPT_POLLING
    MTLK_INIT_STEP(mmb_drv, MMB_IRQ_START, MTLK_OBJ_PTR(obj),
                   _mmb_drv_irq_start, (obj));
#if defined(CPTCFG_IWLWAV_G6_FPGA_IRAM_TEST)
    printk("after _mmb_drv_irq_start\n");
    hw_mmb_iram_rw_test(hw);
#endif
#else /* CPTCFG_IWLWAV_USE_INTERRUPT_POLLING */
    ILOG2_S("%s IRQ polling mode", DRV_NAME);
#endif /* not CPTCFG_IWLWAV_USE_INTERRUPT_POLLING */

    MTLK_INIT_STEP(mmb_drv, MMB_CARD_START, MTLK_OBJ_PTR(obj),
                   mtlk_hw_mmb_start_card, (hw));

	/* we are at FW steady-state */
    wave_rcvry_fw_steady_set (obj->bar1, TRUE);

#if defined(CPTCFG_IWLWAV_G6_FPGA_IRAM_TEST)
    printk("after mtlk_hw_mmb_start_card\n");
    hw_mmb_iram_rw_test(hw);
#endif

    MTLK_INIT_STEP(mmb_drv, WAVE_RADIO_INIT, MTLK_OBJ_PTR(obj),
                   wave_radio_init, (radio_descr, obj->dev));

    MTLK_INIT_STEP(mmb_drv, MMB_TXMM_INIT, MTLK_OBJ_PTR(obj),
                   _mmb_txmm_init, (obj->hw_api));

    MTLK_INIT_STEP(mmb_drv, MMB_TXDM_INIT, MTLK_OBJ_PTR(obj),
                   _mmb_txdm_init, (obj->hw_api));

    MTLK_INIT_STEP(mmb_drv, MMB_CARD_START_FINALIZE, MTLK_OBJ_PTR(obj),
                   mtlk_hw_mmb_start_card_finalize, (hw));
#if defined (CPTCFG_IWLWAV_G6_BAR0_TEST)
    hw_mmb_bar0_test(hw, "after mtlk_hw_mmb_start_card_finalize");
#endif

    MTLK_INIT_STEP(mmb_drv, MMB_RADIO_START_FINALIZE, MTLK_OBJ_PTR(obj),
                   wave_radio_init_finalize, (radio_descr));
#if defined (CPTCFG_IWLWAV_G6_BAR0_TEST)
    hw_mmb_bar0_test(hw, "after wave_radio_init_finalize");
#endif

    MTLK_INIT_STEP(mmb_drv, MMB_RADIO_CALIBRATE, MTLK_OBJ_PTR(obj),
                   wave_radio_calibrate, (radio_descr, WAVE_RADIO_NO_RCVRY));

    MTLK_INIT_STEP_VOID(mmb_drv, MMB_MAC_EVENTS_START, MTLK_OBJ_PTR(obj),
                        MTLK_NOACTION, ());

    MTLK_INIT_STEP_VOID(mmb_drv, MMB_CORE_PREPARE_STOP, MTLK_OBJ_PTR(obj),
                        MTLK_NOACTION, ());

    MTLK_INIT_STEP(mmb_drv, MMB_RADIO_MAC80211_INIT, MTLK_OBJ_PTR(obj),
                   wave_radio_mac80211_init, (radio_descr, obj->dev));

    MTLK_INIT_STEP(mmb_drv, MMB_RCVRY_INIT_FINALIZE, MTLK_OBJ_PTR(obj),
                   wave_rcvry_init_finalize, (hw));

  MTLK_INIT_FINALLY(mmb_drv, MTLK_OBJ_PTR(obj))
  MTLK_INIT_RETURN(mmb_drv, MTLK_OBJ_PTR(obj), _mmb_drv_cleanup, (obj, TRUE))
#if defined (CPTCFG_IWLWAV_G6_BAR0_TEST)
  hw_mmb_bar0_test(hw, "after _mmb_drv_init");
#endif
}

#ifdef CPTCFG_IWLWAV_BUS_PCI_PCIE

/**
 * Entry point for PCI device registration procedure
 */
static int
_pci_probe (struct pci_dev *pdev, const struct pci_device_id *ent)
{
  unsigned device_id = ent->device;
  wave_uint card_idx = 0;
  int res = 0;
  mtlk_mmb_drv_t *obj = NULL;

  mtlk_fast_mem_print_info();

  ILOG1_S("PCI %s Initialization", pci_name(pdev));

  if (no_more_ifaces) {
    ELOG_S("Skipping PCI %s initialization - previous interface startup failed", pci_name(pdev));
    return -ENODEV;
  }

  card_idx = wave_hw_mmb_get_current_card_idx();
  if (card_idx >= MTLK_MAX_HW_ADAPTERS_SUPPORTED) {
    mtlk_osal_emergency_print("Card number too big: %u", (uint)card_idx);
    return -ENODEV;
  }

  obj = mtlk_fast_mem_alloc(MTLK_FM_USER_MMBDRV, sizeof(*obj));
  if (NULL == obj) {
    return -ENOMEM;
  }

  ILOG1_DD("CID-%02x: device_id: 0x%04X", card_idx, device_id);
  memset(obj, 0, sizeof(*obj));
  res = _pci_start(pdev, obj, (mtlk_card_type_t)ent->driver_data, device_id, card_idx);
  if (0 != res) {
    goto _pci_probe_error;
  }

  if (obj->g6_pci_aux) {
    mtlk_osal_emergency_print("PCI AUX Initialization finished");
    return res;
  }

  if (MTLK_ERR_OK != _mmb_drv_init(obj, (mtlk_card_type_t)ent->driver_data)) {
    res = 0; /* at this point PCI-card is started even if _mmb_drv_init is failed */
    goto _pci_probe_error;
  }

  /* Signal to FAPI about Calibration File type */
  if (wave_hw_mmb_eeprom_is_production_mode(obj->hw_api->hw))
    wave_prod_nl_send_msg_cal_file_evt(card_idx);

  ILOG1_S("PCI %s Initialization finished", pci_name(pdev));

  return res;

_pci_probe_error:
  ELOG_SD("PCI %s Initialization failure (%d)", pci_name(pdev), res);

  /* Recovery is not related to PCI AUX device */
  if (!obj->g6_pci_aux && wave_rcvry_pci_probe_error_get(obj->bar1)) {

    /* send UNRECOVERABLE_ERROR msg */
    if (wave_rcvry_enabled_get(obj->bar1))
      wave_rcvry_process_msg_unrecovarable_error(obj->bar1);

    /*if recovery kernel panic enabled set panic()*/
    if(wave_rcvry_krnl_panic_enabled(obj->bar1)) {
      char msg[128];
      snprintf(msg, sizeof(msg), "pci probe failed on card '%u'", (uint)card_idx);
      mtlk_osal_kernel_panic(msg);
    }

  } else {
    _pci_stop(pdev, obj);
    mtlk_fast_mem_free(obj);
    ILOG2_S("PCI %s Stop done", pci_name(pdev));
  }
  return res;
}

/**
 * Entry point for PCI device deletion procedure
 */
static void
_pci_remove (struct pci_dev *pdev)
{
  BOOL is_pci_probe_error;
  mtlk_mmb_drv_t *obj = pci_get_drvdata(pdev);

  if (NULL == obj)
    return;

  ILOG2_S("PCI %s CleanUp", pci_name(pdev));

  is_pci_probe_error = wave_rcvry_pci_probe_error_get(obj->bar1);

  if ((FALSE == obj->g6_pci_aux) &&
      (!is_pci_probe_error)) {
    _mmb_drv_cleanup(obj, FALSE);
  }
  wave_rcvry_card_cleanup(obj->bar1);

  _pci_stop(pdev, obj);
  mtlk_fast_mem_free(obj);
  ILOG2_S("PCI %s Stop done", pci_name(pdev));

  ILOG2_S("PCI %s CleanUp finished", pci_name(pdev));
}

static void _pci_shutdown (struct pci_dev *pdev)
{
  _pci_remove(pdev);
}

#endif /* CPTCFG_IWLWAV_BUS_PCI_PCIE */


/**************************************************************
 *  Main MMB driver registration                              *
 **************************************************************/

#ifdef CPTCFG_IWLWAV_BUS_PCI_PCIE

static struct pci_device_id mmb_pci_tbl[] = {
#ifdef CPTCFG_IWLWAV_LINDRV_HW_PCIEG5
  { MTLK_VENDOR_ID_LANTIQ, MTLK_V_WAVE500_B11_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID, 0, 0, MTLK_CARD_PCIEG5 },
#endif
#ifdef CPTCFG_IWLWAV_LINDRV_HW_PCIEG6
  { INTEL_VENDOR_ID,       INTEL_WAVE600_DEVICE_ID,      PCI_ANY_ID, PCI_ANY_ID, 0, 0, MTLK_CARD_PCIEG6 },
  { INTEL_VENDOR_ID,       INTEL_WAVE600_D2_DEVICE_ID,   PCI_ANY_ID, PCI_ANY_ID, 0, 0, MTLK_CARD_PCIEG6 },
#endif
  { 0,}
};

MODULE_DEVICE_TABLE(pci, mmb_pci_tbl);

static struct pci_driver __refdata mmb_pci_driver = {
  .name     = DRV_NAME,
  .id_table = mmb_pci_tbl,
  .probe    = _pci_probe,
  .remove   = _pci_remove,
  .shutdown = _pci_shutdown,
};
#endif /* CPTCFG_IWLWAV_BUS_PCI_PCIE */

struct mtlk_drv_state
{
  int os_res;
  int init_res;
  int osal_init_res;
  MTLK_DECLARE_INIT_STATUS;
};

static struct mtlk_drv_state drv_state = {0};

MTLK_INIT_STEPS_LIST_BEGIN(bus_drv_mod)
  MTLK_INIT_STEPS_LIST_ENTRY(bus_drv_mod, DRV_DFG_INIT)
  MTLK_INIT_STEPS_LIST_ENTRY(bus_drv_mod, DRV_RCVRY_INIT)
#ifdef CPTCFG_IWLWAV_PMCU_SUPPORT
  MTLK_INIT_STEPS_LIST_ENTRY(bus_drv_mod, DRV_PMCU_INIT)
#endif /* CPTCFG_IWLWAV_PMCU_SUPPORT */
#ifdef CPTCFG_IWLWAV_BUS_PCI_PCIE
  MTLK_INIT_STEPS_LIST_ENTRY(bus_drv_mod, DRV_PCI_REGISTER)
#endif /* CPTCFG_IWLWAV_BUS_PCI_PCIE */
MTLK_INIT_INNER_STEPS_BEGIN(bus_drv_mod)
MTLK_INIT_STEPS_LIST_END(bus_drv_mod);

static void
__exit_module (void)
{
  MTLK_CLEANUP_BEGIN(bus_drv_mod, MTLK_OBJ_PTR(&drv_state))
#ifdef CPTCFG_IWLWAV_BUS_PCI_PCIE
    MTLK_CLEANUP_STEP(bus_drv_mod, DRV_PCI_REGISTER, MTLK_OBJ_PTR(&drv_state),
                      pci_unregister_driver, (&mmb_pci_driver))
#endif /* CPTCFG_IWLWAV_BUS_PCI_PCIE */
#ifdef CPTCFG_IWLWAV_PMCU_SUPPORT
    MTLK_CLEANUP_STEP(bus_drv_mod, DRV_PMCU_INIT, MTLK_OBJ_PTR(&drv_state),
                      wv_PMCU_Cleanup, ());
#endif /* CPTCFG_IWLWAV_PMCU_SUPPORT */
    MTLK_CLEANUP_STEP(bus_drv_mod, DRV_RCVRY_INIT, MTLK_OBJ_PTR(&drv_state),
                      wave_rcvry_cleanup, ());
    MTLK_CLEANUP_STEP(bus_drv_mod, DRV_DFG_INIT, MTLK_OBJ_PTR(&drv_state),
                      mtlk_dfg_cleanup, ());
  MTLK_CLEANUP_END(bus_drv_mod, MTLK_OBJ_PTR(&drv_state))
}

static int
__init_module (void)
{
  int i;

  /* Go over dual_pci params array */
  dual_pci_over_all = FALSE;
  for (i = 0; i < MTLK_MAX_HW_ADAPTERS_SUPPORTED; i++) {
    dual_pci_over_all |= !!dual_pci[i];
  }

  MTLK_INIT_TRY(bus_drv_mod, MTLK_OBJ_PTR(&drv_state))
    MTLK_INIT_STEP(bus_drv_mod, DRV_DFG_INIT, MTLK_OBJ_PTR(&drv_state),
                   mtlk_dfg_init, ());
    MTLK_INIT_STEP_VOID(bus_drv_mod, DRV_RCVRY_INIT, MTLK_OBJ_PTR(&drv_state),
                        wave_rcvry_init, ());
#ifdef CPTCFG_IWLWAV_PMCU_SUPPORT
    MTLK_INIT_STEP(bus_drv_mod, DRV_PMCU_INIT, MTLK_OBJ_PTR(&drv_state),
                   wv_PMCU_Create, ());
#endif /* CPTCFG_IWLWAV_PMCU_SUPPORT */
#ifdef CPTCFG_IWLWAV_BUS_PCI_PCIE
    MTLK_INIT_STEP_EX(bus_drv_mod, DRV_PCI_REGISTER, MTLK_OBJ_PTR(&drv_state),
                      pci_register_driver, (&mmb_pci_driver),
                      drv_state.os_res, 0 == drv_state.os_res, MTLK_ERR_NO_RESOURCES);
#endif /* CPTCFG_IWLWAV_BUS_PCI_PCIE */
  MTLK_INIT_FINALLY(bus_drv_mod, MTLK_OBJ_PTR(&drv_state))
  MTLK_INIT_RETURN(bus_drv_mod, MTLK_OBJ_PTR(&drv_state), __exit_module, ())
}

/* FIXME: workaround: global wlan id
 */

static mtlk_atomic_t g_mmb_drv_wlan_id;

uint32 __MTLK_IFUNC
mtk_mmb_drv_get_free_wlan_id (void)
{
  uint32 wlan_id = mtlk_osal_atomic_get(&g_mmb_drv_wlan_id); /* current */
  mtlk_osal_atomic_add(&g_mmb_drv_wlan_id, 2); /* next */
  return wlan_id;
}

uint32 __MTLK_IFUNC
mtk_mmb_drv_free_wlan_id (void)
{
  int32 wlan_id = mtlk_osal_atomic_sub(&g_mmb_drv_wlan_id, 2); /* current */
  if (wlan_id < 0) {
    MTLK_ASSERT(FALSE);
    ELOG_V("Error in wlan id free");
  }

  return wlan_id;
}

/**
 * Main entry point for MMB driver
 */
static int __init
_init_module (void)
{
  drv_state.osal_init_res = mtlk_osal_init();
  if (MTLK_ERR_OK != drv_state.osal_init_res)
    return -ENODEV;

  mtlk_osal_atomic_set(&g_mmb_drv_wlan_id, 0);

  drv_state.init_res = __init_module();
  return (drv_state.init_res == MTLK_ERR_OK) ? 0 : drv_state.os_res;
}

/**
 * Main termination point for MMB driver
 */
static void __exit
_exit_module (void)
{
  ILOG2_V("Cleanup");
  if (MTLK_ERR_OK == drv_state.init_res) {
    /* Call cleanup only if init succeeds,
     * otherwise it will be called by macros on init itself
     */
    __exit_module();
  }

  if (MTLK_ERR_OK == drv_state.osal_init_res)
  {
    mtlk_osal_cleanup();
  }
}

/* Register entry and termination pints for MMB driver */
module_init(_init_module);
module_exit(_exit_module);

