/******************************************************************************

                               Copyright (c) 2012
                            Lantiq Deutschland GmbH

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/
/*
 * $Id$
 *
 * 
 * 
 * Reliable Multicast routines.
 * 
 *  Originaly written by Artem Migaev & Andriy Tkachuk
 *
 */

#include "mtlkinc.h"

#include <linux/igmp.h>
#include <net/addrconf.h>        /* for ipv6_addr_is_multicast() */

#include "mcast.h"
#include "mtlk_df_nbuf.h"

#if (defined MTLK_DEBUG_IPERF_PAYLOAD_RX) || (defined MTLK_DEBUG_IPERF_PAYLOAD_TX)
#include "iperf_debug.h"
#endif

#include "mtlk_df_user_priv.h"
#include "mtlk_df.h"
#include "mtlk_df_priv.h"
#include "mtlk_dfg.h"

#define LOG_LOCAL_GID   GID_MCAST
#define LOG_LOCAL_FID   1

#define MCAST_HASH(ip)                (ip & (MCAST_HASH_SIZE - 1))
#define GET_MCAST_HASH_V4(grp_addr)   (MCAST_HASH((grp_addr)->s_addr))
#define GET_MCAST_HASH_V6(grp_addr)   (MCAST_HASH((grp_addr)->in6_u.u6_addr32[3]))

/* IEEE 1905.1 frames */
#define MTLK_ETH_P_1905_1   0x893A

/*****************************************************************************/
/***                        Module definitions                             ***/
/*****************************************************************************/

/* IPv4*/
static int                  parse_igmp  (mtlk_core_t *nic, struct igmphdr *igmp_header, sta_entry *sta)    __MTLK_INT_HANDLER_SECTION;
static void                 _mtlk_mc_change_to_exclude_ip4  (mtlk_core_t *nic, sta_entry *sta, const struct in_addr *grp_addr);
static void                 _mtlk_mc_change_to_include_ip4  (mtlk_core_t *nic, sta_entry *sta, const struct in_addr *grp_addr, int src_num);
static void                 _mtlk_mc_block_old_sources_ip4  (mtlk_core_t *nic, sta_entry *sta, const struct in_addr *grp_addr, int src_num);
static void                 _mtlk_mc_allow_new_sources_ip4  (mtlk_core_t *nic, sta_entry *sta, const struct in_addr *grp_addr, int src_num);
static void                 _mtlk_mc_add_ip4_sta            (mtlk_core_t *nic, sta_entry *sta, const struct in_addr *grp_addr);
static void                 _mtlk_mc_del_ip4_sta            (mtlk_core_t *nic, sta_entry *sta, const struct in_addr *grp_addr);

static mcast_ip4_grp_t *    _mtlk_mc_add_sta_to_group_unlocked_ip4   (mtlk_core_t *nic, sta_entry *sta, const struct in_addr *grp_addr, mcast_mode_t mode);
static mcast_ip4_grp_t *    _mtlk_mc_del_sta_from_group_unlocked_ip4 (mtlk_core_t *nic, sta_entry *sta, const struct in_addr *grp_addr);
static mcast_ip4_grp_t*     _mtlk_mc_find_ip4_grp    (mcast_ctx_t *mcast, const struct in_addr *grp_addr);
static mcast_ip4_src_t*     _mtlk_mc_find_ip4_src    (mcast_ip4_grp_t *grp, const struct in_addr *src_addr);
static int                  _mtlk_mc_del_ip4_grp     (mcast_ctx_t *mcast, mcast_ip4_grp_t *grp);
static int                  _mtlk_mc_del_ip4_src     (mcast_ip4_src_t *src);
static mcast_ip4_grp_t*     _mtlk_mc_create_ip4_grp  (mcast_ctx_t *mcast, const struct in_addr *grp_addr);
static mcast_ip4_src_t*     _mtlk_mc_create_ip4_src  (mcast_ip4_grp_t *grp, const struct in_addr *src_addr);
static mcast_group_info_t*  _mtlk_mc_find_ip4_group  (mtlk_core_t *nic, const struct in_addr *grp_addr, const struct in_addr *src_addr);
static void                 _mtlk_mc_print_ip4_group (mcast_ip4_grp_t *grp);

/* IPv6 */
static int                  parse_icmp6 (mtlk_core_t *nic, struct icmp6hdr *icmp6_header, uint32 length, sta_entry *sta)  __MTLK_INT_HANDLER_SECTION;
static void                 _mtlk_mc_change_to_include_ip6  (mtlk_core_t *nic, sta_entry *sta, const struct in6_addr *grp_addr, int src_num);
static void                 _mtlk_mc_block_old_sources_ip6  (mtlk_core_t *nic, sta_entry *sta, const struct in6_addr *grp_addr, int src_num);
static void                 _mtlk_mc_allow_new_sources_ip6  (mtlk_core_t *nic, sta_entry *sta, const struct in6_addr *grp_addr, int src_num);
static void                 _mtlk_mc_add_ip6_sta            (mtlk_core_t *nic, sta_entry *sta, const struct in6_addr *grp_addr, BOOL allow_link_local);
static void                 _mtlk_mc_del_ip6_sta            (mtlk_core_t *nic, sta_entry *sta, const struct in6_addr *grp_addr);

static mcast_ip6_grp_t *    _mtlk_mc_add_sta_to_group_unlocked_ip6   (mtlk_core_t *nic, sta_entry *sta, const struct in6_addr *grp_addr, mcast_mode_t mode);
static mcast_ip6_grp_t *    _mtlk_mc_del_sta_from_group_unlocked_ip6 (mtlk_core_t *nic, sta_entry *sta, const struct in6_addr *grp_addr);
static mcast_ip6_grp_t*     _mtlk_mc_find_ip6_grp    (mcast_ctx_t *mcast, const struct in6_addr *grp_addr);
static mcast_ip6_src_t*     _mtlk_mc_find_ip6_src    (mcast_ip6_grp_t *grp, const struct in6_addr *src_addr);
static int                  _mtlk_mc_del_ip6_grp     (mcast_ctx_t *mcast, mcast_ip6_grp_t *grp);
static int                  _mtlk_mc_del_ip6_src     (mcast_ip6_src_t *src);
static mcast_ip6_grp_t*     _mtlk_mc_create_ip6_grp  (mcast_ctx_t *mcast, const struct in6_addr *grp_addr);
static mcast_ip6_src_t*     _mtlk_mc_create_ip6_src  (mcast_ip6_grp_t *grp, const struct in6_addr *src_addr);
static mcast_group_info_t*  _mtlk_mc_find_ip6_group  (mtlk_core_t *nic, const struct in6_addr *grp_addr, const struct in6_addr *src_addr);
static void                 _mtlk_mc_print_ip6_group (mcast_ip6_grp_t *grp);

/* Multicast ranges */
static int           _mtlk_mc_ranges_init_all (mtlk_core_t *nic);
static void          _mtlk_mc_ranges_cleanup_all (mtlk_core_t *nic);


/* MLDv2 headers definitions. Taken from <linux/igmp.h> */
struct mldv2_grec {
  uint8  grec_type;
  uint8  grec_auxwords;
  uint16 grec_nsrcs;
  struct in6_addr grec_mca;
  struct in6_addr grec_src[0];
};

struct mldv2_report {
  uint8  type;
  uint8  resv1;
  uint16 csum;
  uint16 resv2;
  uint16 ngrec;
  struct mldv2_grec grec[0];
};

struct mldv2_query {
  uint8  type;
	uint8  code;
	uint16 csum;
  uint16 max_response;
  uint16 resv1;
  struct in6_addr group;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	uint8 qrv:3,
        suppress:1,
        resv2:4;
#elif defined(__BIG_ENDIAN_BITFIELD)
	uint8 resv2:4,
       suppress:1,
       qrv:3;
#else
#error "Please define endianess!"
#endif
  uint8 qqic;
  uint16 nsrcs;
  struct in6_addr srcs[0];
};


MTLK_INIT_STEPS_LIST_BEGIN(mcast)
  MTLK_INIT_STEPS_LIST_ENTRY(mcast, MCAST_LOCK)
  MTLK_INIT_STEPS_LIST_ENTRY(mcast, MCAST_LOCK_RANGES)
  MTLK_INIT_STEPS_LIST_ENTRY(mcast, MCAST_IP4_RANGES_LIST)
  MTLK_INIT_STEPS_LIST_ENTRY(mcast, MCAST_IP6_RANGES_LIST)
  MTLK_INIT_STEPS_LIST_ENTRY(mcast, MCAST_DEC_REF_STA_LIST)
  MTLK_INIT_STEPS_LIST_ENTRY(mcast, MCAST_RANGES_INIT)
MTLK_INIT_INNER_STEPS_BEGIN(mcast)
MTLK_INIT_STEPS_LIST_END(mcast);

/*****************************************************************************/
/***                        API Implementation                             ***/
/*****************************************************************************/

static const struct in_addr  all_IPv4 = {0};
static const struct in6_addr all_IPv6 = {{{0}}};

static __INLINE void __mtlk_mc_decref_check_sta_list (mcast_ctx_t *mcast)
{
  MTLK_ASSERT(NULL == mcast->decref_sta_list);
}

int __MTLK_IFUNC
mtlk_mc_init (mtlk_core_t *nic)
{
  mcast_ctx_t *mcast;
  MTLK_ASSERT(nic);

  mcast = &nic->mcast;
  memset(mcast, 0, sizeof(mcast_ctx_t));
  MTLK_INIT_TRY(mcast, MTLK_OBJ_PTR(mcast));
    MTLK_INIT_STEP(mcast, MCAST_LOCK, MTLK_OBJ_PTR(mcast),
                   mtlk_osal_lock_init, (&mcast->lock));
    MTLK_INIT_STEP(mcast, MCAST_LOCK_RANGES, MTLK_OBJ_PTR(mcast),
                   mtlk_osal_lock_init, (&mcast->lock_ranges));
    MTLK_INIT_STEP_VOID(mcast, MCAST_IP4_RANGES_LIST, MTLK_OBJ_PTR(mcast),
                        mtlk_dlist_init, (&mcast->ip4_ranges_list));
    MTLK_INIT_STEP_VOID(mcast, MCAST_IP6_RANGES_LIST, MTLK_OBJ_PTR(mcast),
                        mtlk_dlist_init, (&mcast->ip6_ranges_list));
    MTLK_INIT_STEP_VOID(mcast, MCAST_DEC_REF_STA_LIST, MTLK_OBJ_PTR(mcast),
                        MTLK_NOACTION, ());
    MTLK_INIT_STEP(mcast, MCAST_RANGES_INIT, MTLK_OBJ_PTR(mcast),
                        _mtlk_mc_ranges_init_all, (nic));
  MTLK_INIT_FINALLY(mcast, MTLK_OBJ_PTR(mcast));
  MTLK_INIT_RETURN(mcast, MTLK_OBJ_PTR(mcast), mtlk_mc_uninit, (nic));
}

void
mtlk_mc_uninit (mtlk_core_t *nic)
{
  mcast_ctx_t *mcast;
  MTLK_ASSERT(nic);
  mcast = &nic->mcast;
  MTLK_CLEANUP_BEGIN(mcast, MTLK_OBJ_PTR(mcast))
    MTLK_CLEANUP_STEP(mcast, MCAST_RANGES_INIT, MTLK_OBJ_PTR(mcast),
                      _mtlk_mc_ranges_cleanup_all, (nic));
    MTLK_CLEANUP_STEP(mcast, MCAST_DEC_REF_STA_LIST, MTLK_OBJ_PTR(mcast),
                      __mtlk_mc_decref_check_sta_list, (mcast));
    MTLK_CLEANUP_STEP(mcast, MCAST_IP6_RANGES_LIST, MTLK_OBJ_PTR(mcast),
                      mtlk_dlist_cleanup, (&mcast->ip6_ranges_list));
    MTLK_CLEANUP_STEP(mcast, MCAST_IP4_RANGES_LIST, MTLK_OBJ_PTR(mcast),
                      mtlk_dlist_cleanup, (&mcast->ip4_ranges_list));
    MTLK_CLEANUP_STEP(mcast, MCAST_LOCK_RANGES, MTLK_OBJ_PTR(mcast),
                      mtlk_osal_lock_cleanup, (&mcast->lock_ranges));
    MTLK_CLEANUP_STEP(mcast, MCAST_LOCK, MTLK_OBJ_PTR(mcast),
                      mtlk_osal_lock_cleanup, (&mcast->lock));
  MTLK_CLEANUP_END(mcast, MTLK_OBJ_PTR(mcast));
}

static __INLINE void __mc_decref_sta_list_init (mcast_ctx_t *mcast)
{
  MTLK_ASSERT(NULL == mcast->decref_sta_list);

  /* Allocate a new list */
  mcast->decref_sta_list = mtlk_osal_mem_alloc(sizeof(mtlk_slist_t), MTLK_MEM_TAG_MCAST_STAS);
  if (NULL == mcast->decref_sta_list) {
    ELOG_V("Can't allocate memory for Decrement STA reference counter list");
    return;
  }
  mtlk_slist_init(mcast->decref_sta_list);
}

static __INLINE mtlk_slist_t * __mc_decref_sta_list_release (mcast_ctx_t *mcast)
{
  mtlk_slist_t *list = mcast->decref_sta_list;
  mcast->decref_sta_list = NULL;
  return list;
}

static void _mtlk_mc_add_sta_to_decref_list (mtlk_slist_t *decref_sta_list, sta_entry *sta)
{
  mcast_decref_sta_item *item;

  if (NULL == decref_sta_list) {
    WLOG_V("Decrement reference counter of STA under lock because of memory allocation error");
    mtlk_sta_decref(sta);
    return;
  }

  /* Allocate a new entry */
  item = mtlk_osal_mem_alloc(sizeof(mcast_decref_sta_item), MTLK_MEM_TAG_MCAST_STAS);
  if (NULL == item) {
    /* Decrement reference counter of STA under lock */
    WLOG_V("Decrement reference counter of STA under lock because of memory allocation error");
    mtlk_sta_decref(sta);
    return;
  }
  /* Setup entry and add to list */
  item->sta = sta;
  mtlk_slist_push(decref_sta_list, &item->list_entry);
}

static void _mtlk_mc_decref_stas_from_list (mtlk_slist_t *decref_sta_list)
{
  mtlk_slist_entry_t *entry;

  if (NULL == decref_sta_list)
    return;

  /* Decrement reference counter of all STAs and free memory of all list entries */
  entry = mtlk_slist_pop(decref_sta_list);

  while (NULL != entry) {
    mcast_decref_sta_item *item = MTLK_LIST_GET_CONTAINING_RECORD(entry, mcast_decref_sta_item, list_entry);

    mtlk_sta_decref(item->sta);
    mtlk_osal_mem_free(item);
    entry = mtlk_slist_pop(decref_sta_list);
  };
  /* Free memory of a list */
  mtlk_slist_cleanup(decref_sta_list);
  mtlk_osal_mem_free(decref_sta_list);
}

/*****************************************************************************/
/***                        Local STA functions                            ***/
/*****************************************************************************/

#define MTLK_LIST_FIND_ITEM(item, root, compare_func) \
    for (item = root; item != NULL; item = item->next) { \
      if ((compare_func)) \
        break; \
    }

#define MTLK_LIST_DEL_ITEM(item, last_item, root, on_compare, on_delete) \
  do {                                \
    last_item = NULL;                 \
    for (item = root; item != NULL; item = item->next) { \
      if (on_compare)                 \
        break;                        \
      last_item = item;               \
    }                                 \
    if (item == NULL)                 \
      return 0;                       \
    if (last_item == NULL)            \
      root = item->next;              \
    else                              \
      last_item->next = item->next;   \
    on_delete;                        \
    mtlk_osal_mem_free(item);         \
    return 1;                         \
  } while (0);

static __INLINE mcast_sta_t*
__mtlk_mc_find_mcsta (mcast_sta_t *root, sta_entry *sta)
{
  mcast_sta_t *item;
  MTLK_LIST_FIND_ITEM(item, root, (item->sta == sta));
  return item;
}

static __INLINE int
__mtlk_mc_del_mcsta (mcast_sta_t **root, sta_entry *sta)
{
  mcast_sta_t *item, *last_item;
  MTLK_LIST_DEL_ITEM(item, last_item, *root, (item->sta == sta), {});
}

static int
__mtlk_mc_del_mcsta_by_mac (mtlk_core_t *nic, mcast_sta_t **root, const unsigned char *mac)
{
  mcast_sta_t *item, *last_item;
  MTLK_LIST_DEL_ITEM(item, last_item, *root,
      (0==mtlk_osal_compare_eth_addresses(mac, mtlk_sta_get_addr(item->sta)->au8Addr)),
          {_mtlk_mc_add_sta_to_decref_list(nic->mcast.decref_sta_list, item->sta);});
}

static __INLINE mcast_sta_t*
__mtlk_mc_add_mcsta (mcast_sta_t **root, sta_entry *sta)
{
  mcast_sta_t *item = mtlk_osal_mem_alloc(sizeof(mcast_sta_t), MTLK_MEM_TAG_MCAST);
  MTLK_ASSERT(item != NULL);

  if (item) {
    item->next = *root;
    item->sta = sta;
    item->status = MCAST_STA_STATUS_NONE;
    mtlk_sta_incref(item->sta);
    *root = item;
  }
  return item;
}

static BOOL
_mtlk_mc_add_new_mcsta(mcast_sta_t **root, sta_entry *sta)
{
  if (!__mtlk_mc_find_mcsta(*root, sta)) {
    __mtlk_mc_add_mcsta(root, sta);
    return TRUE;
  }
  return FALSE;
}

static __INLINE void
__mtlk_mc_set_mc_addr_type(mtlk_mc_addr_t *grp_mc_addr, ip_type_t type)
{
  /* structure MUST be cleared before usage */
  memset(grp_mc_addr, 0, sizeof(*grp_mc_addr));
  grp_mc_addr->type = type;
}

static void
_mtlk_mc_print_ip4_group(mcast_ip4_grp_t *grp)
{
#if (IWLWAV_RTLOG_MAX_DLEVEL >= 2)
  mcast_sta_t *mcsta;
  if (!grp) return;

  ILOG2_D("--- group: %B ---", htonl(grp->ip4_grp_addr.s_addr));
  ILOG2_V("  List of STA, subscribed to whole group");
  if (grp->info.mcsta) {
    for (mcsta = grp->info.mcsta; mcsta != NULL; mcsta = mcsta->next) {
      ILOG2_Y("    %Y", mcsta->sta->info.addr.au8Addr);
    }
  }
  else {
    ILOG2_V("    -- none --");
  }
  ILOG2_V("  List of sources");
  if (grp->ip4_src_list) {
    mcast_ip4_src_t *src;
    for (src = grp->ip4_src_list; src != NULL; src = src->next) {
      ILOG2_D("    src: %B", htonl(src->ip4_src_addr.s_addr));
      ILOG2_V("    List of STA");
      if (src->info.mcsta) {
        for (mcsta = src->info.mcsta; mcsta != NULL; mcsta = mcsta->next) {
          ILOG2_Y("      %Y", mcsta->sta->info.addr.au8Addr);
        }
      }
      else {
        ILOG2_V("      -- none --");
      }
    }
  }
  else {
    ILOG2_V("    -- none --");
  }
#endif
}

static void
_mtlk_mc_print_ip6_group(mcast_ip6_grp_t *grp)
{
#if (IWLWAV_RTLOG_MAX_DLEVEL >= 2)
  mcast_sta_t *mcsta;
  if (!grp) return;

  ILOG2_K("--- group: %K ---", grp->ip6_grp_addr.s6_addr);
  ILOG2_V("  List of STA, subscribed to whole group");
  if (grp->info.mcsta) {
    for (mcsta = grp->info.mcsta; mcsta != NULL; mcsta = mcsta->next) {
      ILOG2_Y("    %Y", mcsta->sta->info.addr.au8Addr);
    }
  }
  else {
    ILOG2_V("    -- none --");
  }
  ILOG2_V("  List of sources");
  if (grp->ip6_src_list) {
    mcast_ip6_src_t *src;
    for (src = grp->ip6_src_list; src != NULL; src = src->next) {
      ILOG2_K("    src: %K", src->ip6_src_addr.s6_addr);
      ILOG2_V("    List of STA");
      if (src->info.mcsta) {
        for (mcsta = src->info.mcsta; mcsta != NULL; mcsta = mcsta->next) {
          ILOG2_Y("      %Y", mcsta->sta->info.addr.au8Addr);
        }
      }
      else {
        ILOG2_V("      -- none --");
      }
    }
  }
  else {
    ILOG2_V("    -- none --");
  }
#endif
}

int __MTLK_IFUNC
mtlk_mc_parse (mtlk_core_t* nic, struct sk_buff *skb, sta_entry *sta)
{
  int res = -1;
  struct ethhdr *ether_header;

  /* If multicast module is available, IGMP parsing is not required */
  if (mtlk_core_mcast_module_is_available(nic))
    return res;

  /* Check IP content in MAC header (Ethernet II assumed) */
  ether_header = (struct ethhdr *)skb->data;
  switch (ntohs(ether_header->h_proto))
  {
  case ETH_P_IP:
    {
      /* Check IGMP content in IP header */
      struct iphdr *ip_header = (struct iphdr *)
            ((mtlk_handle_t)ether_header + sizeof(struct ethhdr));
      struct igmphdr *igmp_header;
      if (mtlk_df_nbuf_get_data_length(skb) < sizeof(*ether_header) + sizeof(*ip_header)) {
        ILOG1_D("CID-%04x: Frame length is wrong", mtlk_vap_get_oid(nic->vap_handle));
        return MTLK_ERR_NOT_IN_USE;
      }
      igmp_header = (struct igmphdr *)((mtlk_handle_t)ip_header + ip_header->ihl * 4);
      if (mtlk_df_nbuf_get_data_length(skb) < sizeof(*ether_header) + ip_header->ihl * 4 +
        sizeof(*igmp_header)) {
        ILOG1_D("CID-%04x: Frame length is wrong", mtlk_vap_get_oid(nic->vap_handle));
        return MTLK_ERR_NOT_IN_USE;
      }
      if (ip_header->protocol == IPPROTO_IGMP) {
        res = parse_igmp(nic, igmp_header, sta);
      }
      break;
    }
  case ETH_P_IPV6:
    {
      /* Check MLD content in IPv6 header */
      struct ipv6hdr *ipv6_header = (struct ipv6hdr *)
            ((mtlk_handle_t)ether_header + sizeof(struct ethhdr));
      struct ipv6_hopopt_hdr *hopopt_header = (struct ipv6_hopopt_hdr *)
            ((mtlk_handle_t)ipv6_header + sizeof(struct ipv6hdr));
      uint16 *hopopt_data = (uint16 *)
            ((mtlk_handle_t)hopopt_header + 2);
      uint16 *rtalert_data = (uint16 *)
            ((mtlk_handle_t)hopopt_data + 2);
      struct icmp6hdr *icmp6_header;
      if (mtlk_df_nbuf_get_data_length(skb) < sizeof(*ether_header) + sizeof(*ipv6_header) +
        sizeof(*hopopt_header)) {
        ILOG1_D("CID-%04x: Frame length is wrong", mtlk_vap_get_oid(nic->vap_handle));
        return MTLK_ERR_NOT_IN_USE;
      }
      icmp6_header = (struct icmp6hdr *)
        ((mtlk_handle_t)hopopt_header + 8 * (hopopt_header->hdrlen + 1));
      if (mtlk_df_nbuf_get_data_length(skb) < sizeof(*ether_header) + sizeof(*ipv6_header) +
        sizeof(*hopopt_header) + 8 * (hopopt_header->hdrlen + 1) + sizeof(*icmp6_header)) {
        ILOG1_D("CID-%04x: Frame length is wrong", mtlk_vap_get_oid(nic->vap_handle));
        return MTLK_ERR_NOT_IN_USE;
      }
      if (
          (ipv6_header->nexthdr == IPPROTO_HOPOPTS) &&  // hop-by-hop option in IPv6 packet header
          (htons(*hopopt_data) == 0x0502) &&            // Router Alert option in hop-by-hop option header
          (*rtalert_data == 0) &&                       // MLD presence in Router Alert option header
          (hopopt_header->nexthdr == IPPROTO_ICMPV6)    // ICMPv6 next option in hop-by-hop option header
         )
        res = parse_icmp6(nic, icmp6_header,
          mtlk_df_nbuf_get_data_length(skb) - ((wave_addr)icmp6_header - (wave_addr)skb->data),
          sta);
      break;
    }
  default:
    break;
  }
  return res;
}

static int _mtlk_do_send_data_to_sta (mtlk_core_t* nic, mtlk_core_handle_tx_data_t *data, uint32 nbuf_flags)
__MTLK_INT_HANDLER_SECTION;

static int _mtlk_do_send_data_to_sta (mtlk_core_t* nic, mtlk_core_handle_tx_data_t *data, uint32 nbuf_flags)
{
  mtlk_nbuf_t *nbuf = data->nbuf;
  sta_entry   *sta  = data->dst_sta;
  uint32 nbuf_tid = mtlk_df_nbuf_get_priority(nbuf);
  uint32 nbuf_len = mtlk_df_nbuf_get_data_length(nbuf);
  int res;

  ILOG5_V("TX packet");
  res = mtlk_vap_get_core_vft(nic->vap_handle)->handle_tx_data(nic, data, nbuf_flags);

  if (__LIKELY(MTLK_ERR_OK == res))
  {
    /* NOTE: If handle_tx_data() was successful, nbuf can be invalid */
    /* If confirmed (or failed) unicast packet to known STA */
    if (__LIKELY(sta))
    {
      /* Update STA's timestamp on successful (confirmed by ACK) TX */
      mtlk_sta_on_packet_sent(sta, nbuf_len, nbuf_flags);

      nic->pstats.sta_session_tx_packets++;
      if (__LIKELY(nbuf_tid < NTS_TIDS_GEN6))
        nic->pstats.ac_tx_counter[nbuf_tid]++;
      else
        ELOG_D("Invalid priority :%u", nbuf_tid);

      if (nbuf_flags & MTLK_NBUFF_FORWARD) {
        nic->pstats.fwd_tx_packets++;
        nic->pstats.fwd_tx_bytes += nbuf_len;
      }

      /* reset consecutive drops counter */
      nic->pstats.tx_cons_drop_cnt = 0;

      mtlk_sta_decref(sta);
    }
    else if (mtlk_vap_is_ap(nic->vap_handle))
    {
      MTLK_ASSERT(0 == (nbuf_flags & MTLK_NBUFF_UNICAST));
      nic->pstats.tx_bcast_nrmcast++;
    }
  }
  else {
    /* In case of TX error nbuf is valid */
    mtlk_core_record_xmit_err(nic, nbuf, nbuf_flags);
    if (sta) {
      mtlk_sta_on_packet_dropped(sta, MTLK_TX_DISCARDED_SQ_OVERFLOW);
      mtlk_sta_decref(sta);
    } else {
      mtlk_core_inc_cnt(nic, MTLK_CORE_CNT_TX_PACKETS_DISCARDED_SQ_OVERFLOW);
    }

    /* drop buffer */
    if (MTLK_ERR_DC_DP_XMIT != res) {
      mtlk_df_nbuf_free(nbuf);
    }
  }

  return res;
}

int __MTLK_IFUNC
mtlk_mc_transmit (mtlk_core_t* nic, mtlk_core_handle_tx_data_t *tx_data, uint32 nbuf_flags, int bridge_mode, sta_entry *src_sta)
{
  int res = MTLK_ERR_OK;
  const sta_entry *sta = NULL;
  mtlk_stadb_iterator_t iter;

#ifdef MTLK_DEBUG_IPERF_PAYLOAD_TX
  debug_ooo_analyze_packet(FALSE, tx_data->nbuf, 0);
#endif

  /* analyze and adjust packet's priority */
  qos_adjust_priority(nic->vap_handle, nic->dscp_table, tx_data->nbuf);

  /* for WDS bridge on AP, clone broadcast packets to all peer APs */
  if ((BR_MODE_WDS == bridge_mode) && (nbuf_flags & (MTLK_NBUFF_BROADCAST | MTLK_NBUFF_MULTICAST)))
  {
    int wds_sta_num;
    mtlk_core_handle_tx_data_t clone_data;

    /* copy rx_if's into cloned buffer tx_data */
    clone_data.rx_if = tx_data->rx_if;
    clone_data.rx_subif = tx_data->rx_subif;

    wds_sta_num = 0;

    /*
     * When repeater bridge sends b-cast/m-cast frames to AP via VSTA AP will
     * distribute frames as 3addr mode. VSTA will get the frame again after header conversion
     * in 802.3 with source addr of its bridge or a PC behind the bridge. Therefore driver
     * does not know it needs to drop the packet.
     *
     * Therefore, FW drops 3addr b-cast and multicast frames for 4addr VSTA interfaces.
     *
     * Clone broadcast packets to all 4addr stations (does not include WDS peer APs) aside from the
     * station, which sent the frame so 4addr VSTA does not lose b-cat/m-cast frames it needs to get.
     */
    sta = mtlk_stadb_iterate_first(&nic->slow_ctx->stadb, &iter);
    if (sta) {
      do {
        /* ignore sending to itself or to non-4addr clients */
        if ((sta->peer_ap || mtlk_sta_is_4addr(sta)) && (sta != src_sta)){
          clone_data.dst_sta = (sta_entry *)sta;
          wds_sta_num++;

          /* STA is other than source STA*/
          clone_data.nbuf = mtlk_df_nbuf_clone_no_priv(tx_data->nbuf);

          if (!clone_data.nbuf) {
            mtlk_sta_on_packet_dropped(clone_data.dst_sta, MTLK_TX_DISCARDED_DRV_NO_RESOURCES);
            res = MTLK_ERR_NO_MEM;
            break;
          }
          /* Adjust cloned packet's priority */
          mtlk_df_nbuf_set_priority(clone_data.nbuf, mtlk_df_nbuf_get_priority(tx_data->nbuf));

          mtlk_sta_incref(clone_data.dst_sta);

          ILOG3_Y("Broadcast packet sending to 4addr sta %Y", mtlk_sta_get_addr(clone_data.dst_sta));
          res = _mtlk_do_send_data_to_sta(nic, &clone_data, nbuf_flags);
        }
        sta = mtlk_stadb_iterate_next(&iter);
      } while (sta);
      mtlk_stadb_iterate_done(&iter);
    } /* End of "for each 4addr client" */

    /* Check is any non-wds stations to send broadcast */
    if (0 == (mtlk_stadb_get_sta_cnt(&nic->slow_ctx->stadb) - wds_sta_num)){
      /* drop original buffer*/
      mtlk_df_nbuf_free(tx_data->nbuf);
      return MTLK_ERR_OK;
    }

  } /* End of broadcasting over WDS */

  res = _mtlk_do_send_data_to_sta(nic, tx_data, nbuf_flags);
  return res;
}

void __MTLK_IFUNC
mtlk_mc_add_sta (mtlk_core_t *nic, sta_entry *sta)
{
  struct in6_addr ip_addr;

  /* if multicast module is available, wlan driver does not manage MC groups by itself */
  if (mtlk_core_mcast_module_is_available(nic))
    return;

  /* Add the station to the special all-node multicast group
     to forward Router Advertisement frames when DGAF is disabled */
  ip_addr.in6_u.u6_addr32[0] = 0xFF020000;
  ip_addr.in6_u.u6_addr32[1] = 0x00000000;
  ip_addr.in6_u.u6_addr32[2] = 0x00000000;
  ip_addr.in6_u.u6_addr32[3] = 0x00000001;

  _mtlk_mc_add_ip6_sta(nic, sta, &ip_addr, TRUE);
}

void __MTLK_IFUNC
mtlk_mc_drop_sta (mtlk_core_t* nic, sta_entry *sta)
{
  int i;
  mtlk_mc_addr_t grp_mc_addr;
  mtlk_slist_t *decref_sta_list;
  IEEE_ADDR mac = *mtlk_sta_get_addr(sta);
  BOOL no_mcast_module = !mtlk_core_mcast_module_is_available(nic);

  /* RM option disabled or not AP */
  if (!mtlk_vap_is_ap(nic->vap_handle))
    return;

  mtlk_osal_lock_acquire(&nic->mcast.lock);
  __mc_decref_sta_list_init(&nic->mcast);

  /* IPv4 hash */
  __mtlk_mc_set_mc_addr_type(&grp_mc_addr, MTLK_IPv4);

  for (i = 0; i < MCAST_HASH_SIZE; i++) {
    mcast_ip4_grp_t *grp, *grp_next;
    for (grp = nic->mcast.mcast_ip4_hash[i]; grp != NULL;) {
      mcast_ip4_src_t *src, *src_next;

      grp_next = grp->next;
      grp_mc_addr.grp_ip.ip4_addr = grp->ip4_grp_addr;

      for (src = grp->ip4_src_list; src != NULL;) {
        src_next = src->next;

        /* STEP 1: delete STA by mac from sources list */
        __mtlk_mc_del_mcsta_by_mac(nic, &src->info.mcsta, mac.au8Addr);

        /* STEP 2: release group_id, if last STA was removed */
        if (src->info.mcsta == NULL) {
          grp_mc_addr.src_ip.ip4_addr = src->ip4_src_addr;
          mtlk_core_mcast_handle_grid(nic, &grp_mc_addr, MTLK_GRID_DEL, src->info.grp_id);
          _mtlk_mc_del_ip4_src(src);
        }
        src = src_next;
      }

      /* STEP 3: delete STA by mac from group list */
      __mtlk_mc_del_mcsta_by_mac(nic, &grp->info.mcsta, mac.au8Addr);

      /* STEP 4: delete group if no more sta's */
      if ((grp->info.mcsta == NULL) && (grp->ip4_src_list == NULL)) {
        if (no_mcast_module) {
          /* STEP 4a: release group_id, if last STA was removed */
          grp_mc_addr.src_ip.ip4_addr = all_IPv4;
          mtlk_core_mcast_handle_grid(nic, &grp_mc_addr, MTLK_GRID_DEL, grp->info.grp_id);
        }
        _mtlk_mc_del_ip4_grp(&nic->mcast, grp);
      }

      grp = grp_next;
    }
  }

  /* IPv6 hash */
  __mtlk_mc_set_mc_addr_type(&grp_mc_addr, MTLK_IPv6);

  for (i = 0; i < MCAST_HASH_SIZE; i++) {
    mcast_ip6_grp_t *grp, *grp_next;
    for (grp = nic->mcast.mcast_ip6_hash[i]; grp != NULL;) {
      mcast_ip6_src_t *src, *src_next;

      grp_next = grp->next;
      grp_mc_addr.grp_ip.ip6_addr = grp->ip6_grp_addr;

      for (src = grp->ip6_src_list; src != NULL;) {
        src_next = src->next;

        /* STEP 1: delete STA by mac from sources list */
        __mtlk_mc_del_mcsta_by_mac(nic, &src->info.mcsta, mac.au8Addr);

        /* STEP 2: release group_id, if last STA was removed */
        if (src->info.mcsta == NULL) {
          grp_mc_addr.src_ip.ip6_addr = src->ip6_src_addr;
          mtlk_core_mcast_handle_grid(nic, &grp_mc_addr, MTLK_GRID_DEL, src->info.grp_id);
          _mtlk_mc_del_ip6_src(src);
        }
        src = src_next;
      }

      /* STEP 3: delete STA by mac from group list */
      __mtlk_mc_del_mcsta_by_mac(nic, &grp->info.mcsta, mac.au8Addr);

      /* STEP 4: delete group if no more sta's */
      if ((grp->info.mcsta == NULL) && (grp->ip6_src_list == NULL)) {
        if (no_mcast_module) {
          /* STEP 4a: release group_id, if last STA was removed */
          grp_mc_addr.src_ip.ip6_addr = all_IPv6;
          mtlk_core_mcast_handle_grid(nic, &grp_mc_addr, MTLK_GRID_DEL, grp->info.grp_id);
        }
        _mtlk_mc_del_ip6_grp(&nic->mcast, grp);
      }

      grp = grp_next;
    }
  }

  decref_sta_list = __mc_decref_sta_list_release(&nic->mcast);
  mtlk_osal_lock_release(&nic->mcast.lock);
  _mtlk_mc_decref_stas_from_list(decref_sta_list);

  ILOG1_Y("STA %Y dropped", mac.au8Addr);

  return;
}



void __MTLK_IFUNC
mtlk_mc_restore_mac (struct sk_buff *skb)
{
  struct ethhdr *ether_header = (struct ethhdr *)skb->data;

  switch (ntohs(ether_header->h_proto))
  {
  case ETH_P_IP:
    {
      struct in_addr ip_addr;
      struct iphdr *ip_header = (struct iphdr *)
          ((mtlk_handle_t)ether_header + sizeof(struct ethhdr));
      
      /* do nothing if destination address is:
       *   - not multicast;
       *   - link local multicast.
       */
      if (!mtlk_osal_ipv4_is_multicast(ip_header->daddr) || 
           mtlk_osal_ipv4_is_local_multicast(ip_header->daddr))
        return;
  
      ip_addr.s_addr = ntohl(ip_header->daddr);

      ether_header->h_dest[0] = 0x01;
      ether_header->h_dest[1] = 0x00;
      ether_header->h_dest[2] = 0x5E;
      ether_header->h_dest[3] = (ip_addr.s_addr >> 16) & 0x7F;
      ether_header->h_dest[4] = (ip_addr.s_addr >> 8) & 0xFF;
      ether_header->h_dest[5] = ip_addr.s_addr & 0xFF;

      break;
    }
  case ETH_P_IPV6:
    {
      struct in6_addr ip_addr;
      struct ipv6hdr *ip_header = (struct ipv6hdr *)
          ((mtlk_handle_t)ether_header + sizeof(struct ethhdr));

      /* do nothing if destination address is not multicast */
      if (ipv6_addr_is_multicast(&ip_header->daddr))
        return;

      ip_addr.in6_u.u6_addr32[0] = ntohl(ip_header->daddr.in6_u.u6_addr32[0]);
      ip_addr.in6_u.u6_addr32[1] = ntohl(ip_header->daddr.in6_u.u6_addr32[1]);
      ip_addr.in6_u.u6_addr32[2] = ntohl(ip_header->daddr.in6_u.u6_addr32[2]);
      ip_addr.in6_u.u6_addr32[3] = ntohl(ip_header->daddr.in6_u.u6_addr32[3]);

      // Check link local multicast space FF02::/16
      if ((ip_addr.in6_u.u6_addr32[0] & 0xFF0F0000) == 0xFF020000)
        return;

      ether_header->h_dest[0] = 0x33;
      ether_header->h_dest[1] = 0x33;
      ether_header->h_dest[2] = ip_addr.in6_u.u6_addr8[12];
      ether_header->h_dest[3] = ip_addr.in6_u.u6_addr8[13];
      ether_header->h_dest[4] = ip_addr.in6_u.u6_addr8[14];
      ether_header->h_dest[5] = ip_addr.in6_u.u6_addr8[15];

      break;
    }
  default:
    break;
  }
}

static int
_mtlk_mc_dump_mcsta(mtlk_clpb_t *clpb, mcast_sta_t *mcsta)
{
  int                           res = MTLK_ERR_OK;
  sta_entry                     *sta;
  mtlk_mc_igmp_tbl_mac_item_t   igmp_mac_item;

  for (; mcsta != NULL; mcsta = mcsta->next) {
    sta  = mcsta->sta;
    igmp_mac_item.header.type = MTLK_MC_MAC_ADDR;
    igmp_mac_item.addr = *mtlk_sta_get_addr(sta);

    res = mtlk_clpb_push(clpb, &igmp_mac_item, sizeof(igmp_mac_item));
    if (MTLK_ERR_OK != res) {
      return res;
    }
  }
  return res;
}

int __MTLK_IFUNC
mtlk_mc_dump_groups (mtlk_core_t *nic, mtlk_clpb_t *clpb)
{
  int                           res = MTLK_ERR_OK;
  int                           idx;
  BOOL                          no_mcast_module = !mtlk_core_mcast_module_is_available(nic);;

  /* IPv4 hash */
  for (idx = 0; idx < MCAST_HASH_SIZE; idx++) {
    mcast_ip4_grp_t                 *grp;
    mcast_ip4_src_t                 *src;
    mtlk_mc_igmp_tbl_ipv4_item_t  igmp_ipv4_item;

    for (grp = nic->mcast.mcast_ip4_hash[idx]; grp != NULL; grp = grp->next) {

      /* Store IPv4 MC group address item */
      igmp_ipv4_item.header.type = MTLK_MC_IPV4_GRP_ADDR;
      igmp_ipv4_item.addr = grp->ip4_grp_addr;
      igmp_ipv4_item.grp_id = grp->info.grp_id;

      res = mtlk_clpb_push(clpb, &igmp_ipv4_item, sizeof(igmp_ipv4_item));
      if (MTLK_ERR_OK != res) {
        goto err_push;
      }

      if (no_mcast_module) {
        /* Store MAC addresses of IPv4 MC group members */
        res = _mtlk_mc_dump_mcsta(clpb, grp->info.mcsta);
        if (MTLK_ERR_OK != res) {
          goto err_push;
        }
      }
      else {
        for (src = grp->ip4_src_list; src != NULL; src = src->next) {

          /* Store IPv4 MC group address item */
          igmp_ipv4_item.header.type = MTLK_MC_IPV4_SRC_ADDR;
          igmp_ipv4_item.addr = src->ip4_src_addr;
          igmp_ipv4_item.grp_id = src->info.grp_id;

          res = mtlk_clpb_push(clpb, &igmp_ipv4_item, sizeof(igmp_ipv4_item));
          if (MTLK_ERR_OK != res) {
            goto err_push;
          }

          /* Store MAC addresses of IPv4 MC group:source members */
          res = _mtlk_mc_dump_mcsta(clpb, src->info.mcsta);
          if (MTLK_ERR_OK != res) {
            goto err_push;
          }
        }
      }
    }
  }

  /* IPv6 hash */
  for (idx = 0; idx < MCAST_HASH_SIZE; idx++) {
    mcast_ip6_grp_t                 *grp;
    mcast_ip6_src_t                 *src;
    mtlk_mc_igmp_tbl_ipv6_item_t  igmp_ipv6_item;

    for (grp = nic->mcast.mcast_ip6_hash[idx]; grp != NULL; grp = grp->next) {

      /* Store IPv6 MC group address item */
      igmp_ipv6_item.header.type = MTLK_MC_IPV6_GRP_ADDR;
      igmp_ipv6_item.addr = grp->ip6_grp_addr;
      igmp_ipv6_item.grp_id = grp->info.grp_id;

      res = mtlk_clpb_push(clpb, &igmp_ipv6_item, sizeof(igmp_ipv6_item));
      if (MTLK_ERR_OK != res) {
        goto err_push;
      }

      if (no_mcast_module) {
        /* Store MAC addresses of IPv6 MC group members */
        res = _mtlk_mc_dump_mcsta(clpb, grp->info.mcsta);
        if (MTLK_ERR_OK != res) {
          goto err_push;
        }
      }
      else {
        for (src = grp->ip6_src_list; src != NULL; src = src->next) {

          /* Store IPv6 MC group address item */
          igmp_ipv6_item.header.type = MTLK_MC_IPV6_SRC_ADDR;
          igmp_ipv6_item.addr = src->ip6_src_addr;
          igmp_ipv6_item.grp_id = src->info.grp_id;

          res = mtlk_clpb_push(clpb, &igmp_ipv6_item, sizeof(igmp_ipv6_item));
          if (MTLK_ERR_OK != res) {
            goto err_push;
          }

          /* Store MAC addresses of IPv6 MC group:source members */
          res = _mtlk_mc_dump_mcsta(clpb, src->info.mcsta);
          if (MTLK_ERR_OK != res) {
            goto err_push;
          }
        }
      }
    }
  }

  goto finish;

err_push:
   mtlk_clpb_purge(clpb);
finish:
   return res;
}

/*****************************************************************************/
/***                        Local IPv4 functions                           ***/
/*****************************************************************************/

static mcast_ip4_grp_t*
_mtlk_mc_create_ip4_grp (mcast_ctx_t *mcast, const struct in_addr *grp_addr)
{
  unsigned hash = GET_MCAST_HASH_V4(grp_addr);
  mcast_ip4_grp_t *grp = mtlk_osal_mem_alloc(sizeof(mcast_ip4_grp_t), MTLK_MEM_TAG_MCAST);
  MTLK_ASSERT(grp != NULL);
  if (grp) {
    grp->ip4_grp_addr = *grp_addr;
    grp->ip4_src_list = NULL;
    grp->next = mcast->mcast_ip4_hash[hash];
    mcast->mcast_ip4_hash[hash] = grp;
    grp->info.mcsta = NULL;
    grp->info.grp_id = MCAST_GROUP_UNDEF;
    grp->info.mode = MCAST_NO_MODE;
  }
  return grp;
}

static int
_mtlk_mc_del_ip4_src (mcast_ip4_src_t *src)
{
  mcast_ip4_src_t *item, *last_item;
  MTLK_LIST_DEL_ITEM(item, last_item, src->ip4_grp->ip4_src_list, (item == src), {});
}

static mcast_ip4_src_t*
_mtlk_mc_create_ip4_src (mcast_ip4_grp_t *grp, const struct in_addr *src_addr)
{
  mcast_ip4_src_t *src = mtlk_osal_mem_alloc(sizeof(mcast_ip4_src_t), MTLK_MEM_TAG_MCAST);
  MTLK_ASSERT(src != NULL);
  if (src) {
    src->ip4_grp = grp;
    src->next = grp->ip4_src_list;
    grp->ip4_src_list = src;
    src->ip4_src_addr = *src_addr;
    src->info.mcsta = NULL;
    src->info.grp_id = MCAST_GROUP_UNDEF;
  }
  return src;
}

static mcast_ip4_grp_t *
_mtlk_mc_add_sta_to_group_unlocked_ip4 (mtlk_core_t *nic, sta_entry *sta, const struct in_addr *grp_addr, mcast_mode_t mode)
{
  mcast_ip4_grp_t *grp;
  mtlk_mc_addr_t grp_mc_addr;
  mcast_ctx_t *mcast = &nic->mcast;

  __mtlk_mc_set_mc_addr_type(&grp_mc_addr, MTLK_IPv4);
  grp_mc_addr.grp_ip.ip4_addr = *grp_addr;
  grp_mc_addr.src_ip.ip4_addr = all_IPv4;

  /* STEP 1: get/create mcast group entry*/
  grp = _mtlk_mc_find_ip4_grp(mcast, grp_addr);
  if (grp) {
    /* Add to group only if interface is in requested mode */
    if ((mode != MCAST_NO_MODE) && (mode != grp->info.mode)) return grp;
  } else {
    /* Create entry for multicast group address */
    grp = _mtlk_mc_create_ip4_grp(mcast, grp_addr);
    if (!grp) goto end;
    /* Update mode */
    grp->info.mode = mode;
    /* Request a new group index from HW table. */
    grp->info.grp_id = mtlk_core_mcast_handle_grid(nic, &grp_mc_addr, MTLK_GRID_ADD, MCAST_GROUP_UNDEF);
  }

  /* STEP 2a: add STA to a common list */
  if (_mtlk_mc_add_new_mcsta(&grp->info.mcsta, sta)) {
    /* STEP 2b: add STA to group in FW */
    mtlk_core_mcast_group_id_action_serialized(nic, MTLK_MC_STA_JOIN_GROUP, grp->info.grp_id, (uint8 *) &sta->info.addr, &grp_mc_addr);
  }
  _mtlk_mc_print_ip4_group(grp);

end:
  return grp;
}

static mcast_ip4_grp_t *
_mtlk_mc_del_sta_from_group_unlocked_ip4 (mtlk_core_t *nic, sta_entry *sta, const struct in_addr *grp_addr)
{
  mcast_ip4_grp_t *grp;
  mtlk_mc_addr_t grp_mc_addr;
  mcast_ctx_t *mcast = &nic->mcast;

  /* STEP 1: find group */
  grp = _mtlk_mc_find_ip4_grp(mcast, grp_addr);
  if (!grp)
    goto end;

  ILOG1_D("Deleting STA from ip4 mcast group %B", htonl(grp->ip4_grp_addr.s_addr));

  /* STEP 1: remove sta from list */
  if (!__mtlk_mc_del_mcsta(&grp->info.mcsta, sta))
    goto end;

  /* STEP 2: remove STA from group in FW here, if mcast helper is inactive */
  __mtlk_mc_set_mc_addr_type(&grp_mc_addr, MTLK_IPv4);
  grp_mc_addr.grp_ip.ip4_addr = grp->ip4_grp_addr;
  grp_mc_addr.src_ip.ip4_addr = all_IPv4;
  mtlk_core_mcast_group_id_action_serialized(nic, MTLK_MC_STA_LEAVE_GROUP, grp->info.grp_id, (uint8 *) &sta->info.addr, &grp_mc_addr);
  _mtlk_mc_add_sta_to_decref_list(nic->mcast.decref_sta_list, sta);

  /* STEP 3: delete group if no more sta's */
  MTLK_ASSERT(grp->ip4_src_list == NULL);
  if ((grp->info.mcsta == NULL) && (grp->ip4_src_list == NULL)) {
    /* Release group index in HW table */
    mtlk_core_mcast_handle_grid(nic, &grp_mc_addr, MTLK_GRID_DEL, grp->info.grp_id);
    if (_mtlk_mc_del_ip4_grp(mcast, grp)) {
      ILOG1_V(" - no more STA's in group. Group deleted");
      grp = NULL;
    }
  }

  _mtlk_mc_print_ip4_group(grp);

end:
  return grp;
}

static void
_mtlk_mc_change_to_exclude_ip4 (mtlk_core_t *nic, sta_entry *sta, const struct in_addr *grp_addr)
{
  /* Simplified implementation.
   * Station is added to the multicast group no matter
   * how much sources are excluded */

  mcast_ip4_grp_t *grp;

  MTLK_ASSERT(FALSE == mtlk_core_mcast_module_is_available(nic));

  /* Check link local IPv4 multicast space 224.0.0.0/24 and source station ID */
  if (((grp_addr->s_addr & 0xFFFFFF00) == 0xE0000000) || (sta == NULL))
    return;

  /* Add STA to group and change mode to EXCLUDE */
  mtlk_osal_lock_acquire(&nic->mcast.lock);
  grp = _mtlk_mc_add_sta_to_group_unlocked_ip4(nic, sta, grp_addr, MCAST_NO_MODE);
  if (grp) {
    /* mode is required for processing IGMPV3_ALLOW_NEW_SOURCES and IGMPV3_BLOCK_OLD_SOURCES */
    grp->info.mode = MCAST_EXCLUDE_MODE;
  }
  _mtlk_mc_print_ip4_group(grp);
  mtlk_osal_lock_release(&nic->mcast.lock);
}

static void
_mtlk_mc_change_to_include_ip4 (mtlk_core_t *nic, sta_entry *sta, const struct in_addr *grp_addr, int src_num)
{
  /* Simplified implementation.
   * Station is removed from the multicast group only if no sources included.
   * Otherwise station is added to the multicast group */

  mcast_ip4_grp_t *grp;
  mtlk_slist_t *decref_sta_list;

  MTLK_ASSERT(FALSE == mtlk_core_mcast_module_is_available(nic));

  /* Check link local IPv4 multicast space 224.0.0.0/24 and source station ID */
  if (((grp_addr->s_addr & 0xFFFFFF00) == 0xE0000000) || (sta == NULL))
    return;

  mtlk_osal_lock_acquire(&nic->mcast.lock);
  __mc_decref_sta_list_init(&nic->mcast);
  if (src_num == 0)
    grp = _mtlk_mc_del_sta_from_group_unlocked_ip4(nic, sta, grp_addr);
  else
    grp = _mtlk_mc_add_sta_to_group_unlocked_ip4(nic, sta, grp_addr, MCAST_NO_MODE);

  if (grp) {
    /* mode is required for the correct processing of
     * IGMPV3_ALLOW_NEW_SOURCES and IGMPV3_BLOCK_OLD_SOURCES */
    grp->info.mode = MCAST_INCLUDE_MODE;
  }
  _mtlk_mc_print_ip4_group(grp);

  decref_sta_list = __mc_decref_sta_list_release(&nic->mcast);
  mtlk_osal_lock_release(&nic->mcast.lock);
  _mtlk_mc_decref_stas_from_list(decref_sta_list);
}

static void
_mtlk_mc_allow_new_sources_ip4 (mtlk_core_t *nic, sta_entry *sta, const struct in_addr *grp_addr, int src_num)
{
  /* Simplified implementation.
   * if mode == INCLUDE: Station added to the multicast group no matter
   * how much sources are in IGMPv3 message.
   * if mode == EXCLUDE: do nothing
   */

  if (src_num == 0)
    return;

  /* Check link local IPv4 multicast space 224.0.0.0/24 and source station ID */
  if (((grp_addr->s_addr & 0xFFFFFF00) == 0xE0000000) || (sta == NULL))
    return;

  mtlk_osal_lock_acquire(&nic->mcast.lock);
  _mtlk_mc_add_sta_to_group_unlocked_ip4(nic, sta, grp_addr, MCAST_INCLUDE_MODE);
  mtlk_osal_lock_release(&nic->mcast.lock);
}


static void
_mtlk_mc_block_old_sources_ip4 (mtlk_core_t *nic, sta_entry *sta, const struct in_addr *grp_addr, int src_num)
{
  /* Simplified implementation.
   * if mode == EXCLUDE: Station added to the multicast group no matter
   * how much sources are in IGMPv3 message.
   * if mode == INCLUDE: do nothing
   */

  if (src_num == 0)
    return;

  /* Check link local IPv4 multicast space 224.0.0.0/24 and source station ID */
  if (((grp_addr->s_addr & 0xFFFFFF00) == 0xE0000000) || (sta == NULL))
    return;

  mtlk_osal_lock_acquire(&nic->mcast.lock);
  _mtlk_mc_add_sta_to_group_unlocked_ip4(nic, sta, grp_addr, MCAST_EXCLUDE_MODE);
  mtlk_osal_lock_release(&nic->mcast.lock);
}

static void
_mtlk_mc_add_ip4_sta (mtlk_core_t *nic, sta_entry *sta, const struct in_addr *grp_addr)
{
  MTLK_ASSERT(FALSE == mtlk_core_mcast_module_is_available(nic));

  /* Check link local IPv4 multicast space 224.0.0.0/24 and source station ID */
  if (((grp_addr->s_addr & 0xFFFFFF00) == 0xE0000000) || (sta == NULL))
    return;

  /* Add STA to group */
  mtlk_osal_lock_acquire(&nic->mcast.lock);
  _mtlk_mc_add_sta_to_group_unlocked_ip4(nic, sta, grp_addr, MCAST_NO_MODE);
  mtlk_osal_lock_release(&nic->mcast.lock);
}

static void
_mtlk_mc_del_ip4_sta (mtlk_core_t *nic, sta_entry *sta, const struct in_addr *grp_addr)
{
  mtlk_slist_t *decref_sta_list;

  MTLK_ASSERT(FALSE == mtlk_core_mcast_module_is_available(nic));

  if (!sta )
    return;

  mtlk_osal_lock_acquire(&nic->mcast.lock);
  __mc_decref_sta_list_init(&nic->mcast);
  _mtlk_mc_del_sta_from_group_unlocked_ip4(nic, sta, grp_addr);
  decref_sta_list = __mc_decref_sta_list_release(&nic->mcast);
  mtlk_osal_lock_release(&nic->mcast.lock);
  _mtlk_mc_decref_stas_from_list(decref_sta_list);
}

static int
_mtlk_mc_del_ip4_grp (mcast_ctx_t *mcast, mcast_ip4_grp_t *grp)
{
  unsigned int hash;
  mcast_ip4_grp_t *item, *last_item;

  if (grp == NULL)
    return 0;

  ILOG1_D("Deleting ip4 mcast group %B", htonl(grp->ip4_grp_addr.s_addr));
  hash = GET_MCAST_HASH_V4(&grp->ip4_grp_addr);
  MTLK_LIST_DEL_ITEM(item, last_item, mcast->mcast_ip4_hash[hash], (item == grp), {});
}


static mcast_ip4_grp_t*
_mtlk_mc_find_ip4_grp (mcast_ctx_t *mcast, const struct in_addr *grp_addr)
{
  unsigned int hash = GET_MCAST_HASH_V4(grp_addr);
  mcast_ip4_grp_t *grp;
  /* Find multicast group */
  MTLK_LIST_FIND_ITEM(grp, mcast->mcast_ip4_hash[hash], (grp->ip4_grp_addr.s_addr == grp_addr->s_addr));
  return grp;
}

static mcast_ip4_src_t *
_mtlk_mc_find_ip4_src (mcast_ip4_grp_t *grp, const struct in_addr *src_addr)
{
  mcast_ip4_src_t *src = NULL;
  if (grp) { /* Find source */
    MTLK_LIST_FIND_ITEM(src, grp->ip4_src_list, (src->ip4_src_addr.s_addr == src_addr->s_addr));
  }
  return src;
}

static mcast_group_info_t *
_mtlk_mc_find_ip4_group (mtlk_core_t *nic, const struct in_addr *grp_addr, const struct in_addr *src_addr)
{
  mcast_ip4_grp_t *grp;
  mcast_ip4_src_t *src;
  mcast_group_info_t *info = NULL;

  if (mtlk_core_mcast_module_is_available(nic)) {
    grp = _mtlk_mc_find_ip4_grp(&nic->mcast, grp_addr);
    src = _mtlk_mc_find_ip4_src(grp, src_addr);
    if (src) info = &src->info;
  }
  else {
    grp = _mtlk_mc_find_ip4_grp(&nic->mcast, grp_addr);
    if (grp) info = &grp->info;
  }

  return info;
}


static int
parse_igmp (mtlk_core_t *nic, struct igmphdr *igmp_header, sta_entry *sta)
{
  unsigned grp_num, src_num;
  unsigned i;
  struct igmpv3_report *report;
  struct igmpv3_grec *record;
  struct in_addr grp_addr;

  grp_addr.s_addr = ntohl(igmp_header->group);

  switch (igmp_header->type) {
  case IGMP_HOST_MEMBERSHIP_QUERY:
    break;
  case IGMP_HOST_MEMBERSHIP_REPORT:
    /* fallthrough */
  case IGMPV2_HOST_MEMBERSHIP_REPORT:
    ILOG1_DY("Membership report: IPv4 %B, sta %Y", htonl(grp_addr.s_addr), mtlk_sta_get_addr(sta)->au8Addr);
    _mtlk_mc_add_ip4_sta(nic, sta, &grp_addr);
    break;
  case IGMP_HOST_LEAVE_MESSAGE:
    ILOG1_DY("Leave group report: IPv4 %B, sta %Y", htonl(grp_addr.s_addr), mtlk_sta_get_addr(sta)->au8Addr);
    _mtlk_mc_del_ip4_sta(nic, sta, &grp_addr);
    break;
  case IGMPV3_HOST_MEMBERSHIP_REPORT:
    report = (struct igmpv3_report *)igmp_header;
    grp_num = ntohs(report->ngrec);
    ILOG1_DY("IGMPv3 report: %d record(s), sta %Y", grp_num, mtlk_sta_get_addr(sta)->au8Addr);
    record = report->grec;
    for (i = 0; i < grp_num; i++) {
      int i_src;
      src_num = ntohs(record->grec_nsrcs);
      for (i_src=0; i_src < src_num; i_src++) {
        ILOG1_DD("  %2d: src %B", i_src, record->grec_src[i_src]); /* network order */
      }

      grp_addr.s_addr = ntohl(record->grec_mca);
      ILOG1_D(" *** IPv4 %B", htonl(grp_addr.s_addr));
      switch (record->grec_type) {
      case IGMPV3_MODE_IS_INCLUDE:
        /* fallthrough */
      case IGMPV3_CHANGE_TO_INCLUDE:
        ILOG1_D(" --- Mode is include, %d source(s)", src_num);
        /* Station removed from the multicast list only if
         * no sources included */
        _mtlk_mc_change_to_include_ip4(nic, sta, &grp_addr, src_num);
        break;
      case IGMPV3_MODE_IS_EXCLUDE:
        /* fallthrough */
      case IGMPV3_CHANGE_TO_EXCLUDE:
        ILOG1_D(" --- Mode is exclude, %d source(s)", src_num);
        /* Station added to the multicast llist no matter
         * how much sources are excluded */
        _mtlk_mc_change_to_exclude_ip4(nic, sta, &grp_addr);
        break;
      case IGMPV3_ALLOW_NEW_SOURCES:
        ILOG1_D(" --- Allow new sources, %d source(s)", src_num);
        _mtlk_mc_allow_new_sources_ip4(nic, sta, &grp_addr, src_num);
        break;
      case IGMPV3_BLOCK_OLD_SOURCES:
        ILOG1_D(" --- Block old sources, %d source(s)", src_num);
        _mtlk_mc_block_old_sources_ip4(nic, sta, &grp_addr, src_num);
        break;
      default:  /* Unknown IGMPv3 record */
        ILOG1_D(" --- Unknown record type %d", record->grec_type);
        break;
      }
      record = (struct igmpv3_grec *)((void *)record +
               sizeof(struct igmpv3_grec) +
               sizeof(struct in_addr) * src_num +
               sizeof(u32) * record->grec_auxwords);
    }
    break;
  default:
    ILOG1_D("Unknown IGMP message type %d", igmp_header->type);
    break;
  }
  return 0;
}


/*****************************************************************************/
/***                        Local IPv6 functions                           ***/
/*****************************************************************************/

static mcast_ip6_grp_t*
_mtlk_mc_create_ip6_grp (mcast_ctx_t *mcast, const struct in6_addr *grp_addr)
{
  unsigned hash = GET_MCAST_HASH_V6(grp_addr);
  mcast_ip6_grp_t *grp = mtlk_osal_mem_alloc(sizeof(mcast_ip6_grp_t), MTLK_MEM_TAG_MCAST);
  MTLK_ASSERT(grp != NULL);
  if (grp) {
    grp->ip6_grp_addr = *grp_addr;
    grp->ip6_src_list = NULL;
    grp->next = mcast->mcast_ip6_hash[hash];
    mcast->mcast_ip6_hash[hash] = grp;
    grp->info.mcsta = NULL;
    grp->info.grp_id = MCAST_GROUP_UNDEF;
    grp->info.mode = MCAST_NO_MODE;
  }
  return grp;
}

static int
_mtlk_mc_del_ip6_src (mcast_ip6_src_t *src)
{
  mcast_ip6_src_t *item, *last_item;
  MTLK_LIST_DEL_ITEM(item, last_item, src->ip6_grp->ip6_src_list, (item == src), {});
}

static mcast_ip6_src_t*
_mtlk_mc_create_ip6_src (mcast_ip6_grp_t *grp, const struct in6_addr *src_addr)
{
  mcast_ip6_src_t *src = mtlk_osal_mem_alloc(sizeof(mcast_ip6_src_t), MTLK_MEM_TAG_MCAST);
  MTLK_ASSERT(src != NULL);
  if (src) {
    src->ip6_grp = grp;
    src->next = grp->ip6_src_list;
    grp->ip6_src_list = src;
    src->ip6_src_addr = *src_addr;
    src->info.mcsta = NULL;
    src->info.grp_id = MCAST_GROUP_UNDEF;
  }
  return src;
}

static mcast_ip6_grp_t *
_mtlk_mc_add_sta_to_group_unlocked_ip6 (mtlk_core_t *nic, sta_entry *sta, const struct in6_addr *grp_addr, mcast_mode_t mode)
{
  mcast_ip6_grp_t *grp;
  mtlk_mc_addr_t grp_mc_addr;
  mcast_ctx_t *mcast = &nic->mcast;

  __mtlk_mc_set_mc_addr_type(&grp_mc_addr, MTLK_IPv6);
  grp_mc_addr.grp_ip.ip6_addr = *grp_addr;
  grp_mc_addr.src_ip.ip6_addr = all_IPv6;

  /* STEP 1: get/create mcast group entry*/
  grp = _mtlk_mc_find_ip6_grp(mcast, grp_addr);
  if (grp) {
    /* Add to group only if interface is in requested mode */
    if ((mode != MCAST_NO_MODE) && (mode != grp->info.mode)) return grp;
  } else {
    /* Create entry for multicast group address */
    grp = _mtlk_mc_create_ip6_grp(mcast, grp_addr);
    if (!grp) goto end;
    /* Update mode */
    grp->info.mode = mode;
    /* Request a new group index from HW table. */
    grp->info.grp_id = mtlk_core_mcast_handle_grid(nic, &grp_mc_addr, MTLK_GRID_ADD, MCAST_GROUP_UNDEF);
  }

  /* STEP 2a: add STA to a common list */
  if (_mtlk_mc_add_new_mcsta(&grp->info.mcsta, sta)) {
    /* STEP 2b: add STA to group in FW */
    mtlk_core_mcast_group_id_action_serialized(nic, MTLK_MC_STA_JOIN_GROUP, grp->info.grp_id, (uint8 *) &sta->info.addr, &grp_mc_addr);
  }
  _mtlk_mc_print_ip6_group(grp);

end:
  return grp;
}

static mcast_ip6_grp_t *
_mtlk_mc_del_sta_from_group_unlocked_ip6 (mtlk_core_t *nic, sta_entry *sta, const struct in6_addr *grp_addr)
{
  mcast_ip6_grp_t *grp;
  mtlk_mc_addr_t grp_mc_addr;
  mcast_ctx_t *mcast = &nic->mcast;

  /* STEP 1: find group */
  grp = _mtlk_mc_find_ip6_grp(mcast, grp_addr);
  if (!grp)
    goto end;

  ILOG1_K("Deleting STA from ip6 mcast group %K", grp->ip6_grp_addr.s6_addr);

  /* STEP 1: remove sta from list */
  if (!__mtlk_mc_del_mcsta(&grp->info.mcsta, sta))
    goto end;

  /* STEP 2: remove STA from group in FW here, if mcast helper is inactive */
  __mtlk_mc_set_mc_addr_type(&grp_mc_addr, MTLK_IPv6);
  grp_mc_addr.grp_ip.ip6_addr = grp->ip6_grp_addr;
  grp_mc_addr.src_ip.ip6_addr = all_IPv6;
  mtlk_core_mcast_group_id_action_serialized(nic, MTLK_MC_STA_LEAVE_GROUP, grp->info.grp_id, (uint8 *) &sta->info.addr, &grp_mc_addr);
  _mtlk_mc_add_sta_to_decref_list(nic->mcast.decref_sta_list, sta);

  /* STEP 3: delete group if no more sta's */
  MTLK_ASSERT(grp->ip6_src_list == NULL);
  if ((grp->info.mcsta == NULL) && (grp->ip6_src_list == NULL)) {
    /* Release group index in HW table */
    mtlk_core_mcast_handle_grid(nic, &grp_mc_addr, MTLK_GRID_DEL, grp->info.grp_id);
    if (_mtlk_mc_del_ip6_grp(mcast, grp)) {
      ILOG1_V(" - no more STA's in group. Group deleted");
      grp = NULL;
    }
  }

  _mtlk_mc_print_ip6_group(grp);

end:
  return grp;
}

static void
_mtlk_mc_change_to_exclude_ip6 (mtlk_core_t *nic, sta_entry *sta, const struct in6_addr *grp_addr)
{
  /* Simplified implementation.
   * Station is added to the multicast group no matter
   * how much sources are excluded */

  mcast_ip6_grp_t *grp;

  MTLK_ASSERT(FALSE == mtlk_core_mcast_module_is_available(nic));

  /* Check link local multicast space FF02::/16 and source station ID */
  if (((grp_addr->in6_u.u6_addr32[0] & 0xFF0F0000) == 0xFF020000) || (sta == NULL))
    return;

  /* Add STA to group and change mode to EXCLUDE */
  mtlk_osal_lock_acquire(&nic->mcast.lock);
  grp = _mtlk_mc_add_sta_to_group_unlocked_ip6(nic, sta, grp_addr, MCAST_NO_MODE);
  if (grp) {
    /* mode is required for processing IGMPV3_ALLOW_NEW_SOURCES and IGMPV3_BLOCK_OLD_SOURCES */
    grp->info.mode = MCAST_EXCLUDE_MODE;
  }
  _mtlk_mc_print_ip6_group(grp);
  mtlk_osal_lock_release(&nic->mcast.lock);
}

static void
_mtlk_mc_change_to_include_ip6 (mtlk_core_t *nic, sta_entry *sta, const struct in6_addr *grp_addr, int src_num)
{
  /* Simplified implementation.
   * Station is removed from the multicast group only if no sources included.
   * Otherwise station is added to the multicast group */

  mcast_ip6_grp_t *grp;
  mtlk_slist_t *decref_sta_list;

  MTLK_ASSERT(FALSE == mtlk_core_mcast_module_is_available(nic));

  /* Check link local multicast space FF02::/16 and source station ID */
  if (((grp_addr->in6_u.u6_addr32[0] & 0xFF0F0000) == 0xFF020000) || (sta == NULL))
    return;

  mtlk_osal_lock_acquire(&nic->mcast.lock);
  __mc_decref_sta_list_init(&nic->mcast);
  if (src_num == 0)
    grp = _mtlk_mc_del_sta_from_group_unlocked_ip6(nic, sta, grp_addr);
  else
    grp = _mtlk_mc_add_sta_to_group_unlocked_ip6(nic, sta, grp_addr, MCAST_NO_MODE);

  if (grp) {
    /* mode is required for the correct processing of
     * IGMPV3_ALLOW_NEW_SOURCES and IGMPV3_BLOCK_OLD_SOURCES */
    grp->info.mode = MCAST_INCLUDE_MODE;
  }
  _mtlk_mc_print_ip6_group(grp);

  decref_sta_list = __mc_decref_sta_list_release(&nic->mcast);
  mtlk_osal_lock_release(&nic->mcast.lock);
  _mtlk_mc_decref_stas_from_list(decref_sta_list);
}

static void
_mtlk_mc_allow_new_sources_ip6 (mtlk_core_t *nic, sta_entry *sta, const struct in6_addr *grp_addr, int src_num)
{
  /* Simplified implementation.
   * if mode == INCLUDE: Station added to the multicast group no matter
   * how much sources are in IGMPv3 message.
   * if mode == EXCLUDE: do nothing
   */

  if (src_num == 0)
    return;

  /* Check link local multicast space FF02::/16 and source station ID */
  if (((grp_addr->in6_u.u6_addr32[0] & 0xFF0F0000) == 0xFF020000) || (sta == NULL))
    return;

  mtlk_osal_lock_acquire(&nic->mcast.lock);
  _mtlk_mc_add_sta_to_group_unlocked_ip6(nic, sta, grp_addr, MCAST_INCLUDE_MODE);
  mtlk_osal_lock_release(&nic->mcast.lock);
}


static void
_mtlk_mc_block_old_sources_ip6(mtlk_core_t *nic, sta_entry *sta, const struct in6_addr *grp_addr, int src_num)
{
  /* Simplified implementation.
   * if mode == EXCLUDE: Station added to the multicast group no matter
   * how much sources are in IGMPv3 message.
   * if mode == INCLUDE: do nothing
   */

  if (src_num == 0)
    return;

  /* Check link local multicast space FF02::/16 and source station ID */
  if (((grp_addr->in6_u.u6_addr32[0] & 0xFF0F0000) == 0xFF020000) || (sta == NULL))
    return;

  mtlk_osal_lock_acquire(&nic->mcast.lock);
  _mtlk_mc_add_sta_to_group_unlocked_ip6(nic, sta, grp_addr, MCAST_EXCLUDE_MODE);
  mtlk_osal_lock_release(&nic->mcast.lock);
}

static void
_mtlk_mc_add_ip6_sta (mtlk_core_t *nic, sta_entry *sta, const struct in6_addr *grp_addr, BOOL allow_link_local)
{
  MTLK_ASSERT(FALSE == mtlk_core_mcast_module_is_available(nic));

  /* Check link local multicast space FF02::/16 and source station ID */
  if ((!allow_link_local && ((grp_addr->in6_u.u6_addr32[0] & 0xFF0F0000) == 0xFF020000)) || (sta == NULL))
    return;

  /* Add STA to group */
  mtlk_osal_lock_acquire(&nic->mcast.lock);
  _mtlk_mc_add_sta_to_group_unlocked_ip6(nic, sta, grp_addr, MCAST_NO_MODE);
  mtlk_osal_lock_release(&nic->mcast.lock);
}

static void
_mtlk_mc_del_ip6_sta (mtlk_core_t *nic, sta_entry *sta, const struct in6_addr *grp_addr)
{
  mtlk_slist_t *decref_sta_list;

  MTLK_ASSERT(FALSE == mtlk_core_mcast_module_is_available(nic));

  if (!sta )
    return;

  mtlk_osal_lock_acquire(&nic->mcast.lock);
  __mc_decref_sta_list_init(&nic->mcast);
  _mtlk_mc_del_sta_from_group_unlocked_ip6(nic, sta, grp_addr);
  decref_sta_list = __mc_decref_sta_list_release(&nic->mcast);
  mtlk_osal_lock_release(&nic->mcast.lock);
  _mtlk_mc_decref_stas_from_list(decref_sta_list);
}

static int
_mtlk_mc_del_ip6_grp (mcast_ctx_t *mcast, mcast_ip6_grp_t *grp)
{
  unsigned int hash;
  mcast_ip6_grp_t *item, *last_item;

  if (grp == NULL)
    return 0;

  ILOG1_K("Deleting ip6 mcast group %K", grp->ip6_grp_addr.s6_addr);
  hash = GET_MCAST_HASH_V6(&grp->ip6_grp_addr);
  MTLK_LIST_DEL_ITEM(item, last_item, mcast->mcast_ip6_hash[hash], (item == grp), {});
}


static mcast_ip6_grp_t*
_mtlk_mc_find_ip6_grp (mcast_ctx_t *mcast, const struct in6_addr *grp_addr)
{
  unsigned int hash = GET_MCAST_HASH_V6(grp_addr);
  mcast_ip6_grp_t *grp;
  /* Find multicast group */
  MTLK_LIST_FIND_ITEM(grp, mcast->mcast_ip6_hash[hash], (ipv6_addr_equal(&grp->ip6_grp_addr, grp_addr)));
  return grp;
}

static mcast_ip6_src_t *
_mtlk_mc_find_ip6_src (mcast_ip6_grp_t *grp, const struct in6_addr *src_addr)
{
  mcast_ip6_src_t *src = NULL;
  if (grp) { /* Find source */
    MTLK_LIST_FIND_ITEM(src, grp->ip6_src_list, (ipv6_addr_equal(&src->ip6_src_addr, src_addr)));
  }
  return src;
}

static mcast_group_info_t *
_mtlk_mc_find_ip6_group (mtlk_core_t *nic, const struct in6_addr *grp_addr, const struct in6_addr *src_addr)
{
  mcast_ip6_grp_t *grp;
  mcast_ip6_src_t *src;
  mcast_group_info_t *info = NULL;

  if (mtlk_core_mcast_module_is_available(nic)) {
    grp = _mtlk_mc_find_ip6_grp(&nic->mcast, grp_addr);
    src = _mtlk_mc_find_ip6_src(grp, src_addr);
    if (src) info = &src->info;
  }
  else {
    grp = _mtlk_mc_find_ip6_grp(&nic->mcast, grp_addr);
    if (grp) info = &grp->info;
  }

  return info;
}


static int
parse_icmp6 (mtlk_core_t *nic, struct icmp6hdr *icmp6_header, uint32 length, sta_entry *sta)
{
  unsigned grp_num, src_num;
  unsigned i;
  struct mldv2_report *report;
  struct mldv2_grec *record;
  struct in6_addr grp_addr, *addr;
  wave_addr end = (wave_addr)icmp6_header + length;
 
  addr = (struct in6_addr *)
    ((mtlk_handle_t)icmp6_header + sizeof(struct icmp6hdr));
  if ((wave_addr)addr + sizeof(*addr) > end) {
    ILOG1_D("CID-%04x: Frame length is wrong", mtlk_vap_get_oid(nic->vap_handle));
    return -1;
  }
  grp_addr.in6_u.u6_addr32[0] = ntohl(addr->in6_u.u6_addr32[0]);
  grp_addr.in6_u.u6_addr32[1] = ntohl(addr->in6_u.u6_addr32[1]);
  grp_addr.in6_u.u6_addr32[2] = ntohl(addr->in6_u.u6_addr32[2]);
  grp_addr.in6_u.u6_addr32[3] = ntohl(addr->in6_u.u6_addr32[3]);

  switch (icmp6_header->icmp6_type) {
  case ICMPV6_MGM_QUERY:
    break;
  case ICMPV6_MGM_REPORT:
    ILOG1_KY("MLD membership report: IPv6 %K, sta %Y", grp_addr.s6_addr, mtlk_sta_get_addr(sta)->au8Addr);
    _mtlk_mc_add_ip6_sta(nic, sta, &grp_addr, FALSE);
    break;
  case ICMPV6_MGM_REDUCTION:
    ILOG1_KY("MLD membership done: IPv6 %K, sta %Y", grp_addr.s6_addr, mtlk_sta_get_addr(sta)->au8Addr);
    _mtlk_mc_del_ip6_sta(nic, sta, &grp_addr);
    break;
  case ICMPV6_MLD2_REPORT:
    report = (struct mldv2_report *)icmp6_header;
    grp_num = ntohs(report->ngrec);
    ILOG1_DY("MLDv2 report: %d record(s), sta %Y", grp_num, mtlk_sta_get_addr(sta)->au8Addr);
    record = report->grec;
    for (i = 0; i < grp_num; i++) {
      if ((wave_addr)record + sizeof(*record) > end) {
        ILOG1_D("CID-%04x: Frame length is wrong", mtlk_vap_get_oid(nic->vap_handle));
        return -1;
      }
      src_num = ntohs(record->grec_nsrcs);
      addr = (struct in6_addr *)
        ((mtlk_handle_t)icmp6_header + sizeof(struct icmp6hdr));
      grp_addr.in6_u.u6_addr32[0] = ntohl(record->grec_mca.in6_u.u6_addr32[0]);
      grp_addr.in6_u.u6_addr32[1] = ntohl(record->grec_mca.in6_u.u6_addr32[1]);
      grp_addr.in6_u.u6_addr32[2] = ntohl(record->grec_mca.in6_u.u6_addr32[2]);
      grp_addr.in6_u.u6_addr32[3] = ntohl(record->grec_mca.in6_u.u6_addr32[3]);
      ILOG1_K(" *** IPv6 %K", grp_addr.s6_addr);
      switch (record->grec_type) {
      case MLD2_MODE_IS_INCLUDE:
        /* fallthrough */
      case MLD2_CHANGE_TO_INCLUDE:
        ILOG1_D(" --- Mode is include, %d source(s)", src_num);
        /* Station removed from the multicast list only if
         * no sources included */
        _mtlk_mc_change_to_include_ip6(nic, sta, &grp_addr, src_num);
        break;
      case MLD2_MODE_IS_EXCLUDE:
        /* fallthrough */
      case MLD2_CHANGE_TO_EXCLUDE:
        ILOG1_D(" --- Mode is exclude, %d source(s)", src_num);
        /* Station added to the multicast llist no matter
         * how much sources are excluded */
        _mtlk_mc_change_to_exclude_ip6(nic, sta, &grp_addr);
        break;
      case MLD2_ALLOW_NEW_SOURCES:
        ILOG1_D(" --- Allow new sources, %d source(s)", src_num);
        _mtlk_mc_allow_new_sources_ip6(nic, sta, &grp_addr, src_num);
        break;
      case MLD2_BLOCK_OLD_SOURCES:
        ILOG1_D(" --- Block old sources, %d source(s)", src_num);
        _mtlk_mc_block_old_sources_ip6(nic, sta, &grp_addr, src_num);
        break;
      default:  /* Unknown MLDv2 record */
        ILOG1_D(" --- Unknown record type %d", record->grec_type);
        break;
      }
      record = (struct mldv2_grec *)((void *)record +
               sizeof(struct mldv2_grec) +
               sizeof(struct in6_addr) * src_num +
               sizeof(u32) * record->grec_auxwords);
    }
    break;
  default:
    ILOG1_D("Unknown ICMPv6/MLD message %d", icmp6_header->icmp6_type);
    break;
  }
  return 0;
}

void __MTLK_IFUNC
mtlk_mc_update_group_id_sta (mtlk_core_t *nic, uint32 grp_id, mc_action_t action, mtlk_mc_addr_t *mc_addr, sta_entry *sta)
{
  /* Update GRP_ID in FW for defined Station.
   *
   * action == MTLK_STA_JOIN:
   *    1) get GRP_ID, which corresponds to multicast address mc_addr, from internal database
   *    2) send ADD_MULTICAST_GROUP request with this GRP_ID to FW
   *
   * action == MTLK_STA_LEAVE:
   *    1) send REMOVE_MULTICAST_GROUP request with "grp_id" to FW
   */
  mcast_group_info_t *group;
  mcast_sta_t *mcsta;

  switch (action) {
  case MTLK_MC_STA_JOIN_GROUP:
    mtlk_osal_lock_acquire(&nic->mcast.lock);

    if (mc_addr->type == MTLK_IPv4) {
      group = _mtlk_mc_find_ip4_group(nic, &mc_addr->grp_ip.ip4_addr, &mc_addr->src_ip.ip4_addr);
      if (!group) {
        goto finish;
      }

      mcsta = __mtlk_mc_find_mcsta(group->mcsta, sta);
      if (!mcsta) {
        ILOG1_D("CID-%04x: station is not found", mtlk_vap_get_oid(nic->vap_handle));
        goto finish;  /* Station is not found */
      }

      if (group->grp_id == MCAST_GROUP_UNDEF) {
        ILOG1_DDD("CID-%04x: GRID is not yet set for IPv4:%B --> %B", mtlk_vap_get_oid(nic->vap_handle),
            htonl(mc_addr->src_ip.ip4_addr.s_addr), htonl(mc_addr->grp_ip.ip4_addr.s_addr));
        goto finish;
      }

      grp_id = group->grp_id;
    } else {
      group = _mtlk_mc_find_ip6_group(nic, &mc_addr->grp_ip.ip6_addr, &mc_addr->src_ip.ip6_addr);
      if (!group) {
        goto finish;
      }

      mcsta = __mtlk_mc_find_mcsta(group->mcsta, sta);
      if (!mcsta) {
        goto finish;  /* Station is not found */
      }

      if (group->grp_id == MCAST_GROUP_UNDEF) {
        ILOG1_DKK("CID-%04x: GRID is not yet set for IPv6:%K --> %K", mtlk_vap_get_oid(nic->vap_handle),
            mc_addr->src_ip.ip6_addr.s6_addr, mc_addr->grp_ip.ip6_addr.s6_addr);
        goto finish;
      }

      grp_id = group->grp_id;
    }

    mtlk_osal_lock_release(&nic->mcast.lock);
    break;

  case MTLK_MC_STA_LEAVE_GROUP:
    if (grp_id == MCAST_GROUP_UNDEF) {
      ILOG1_D("CID-%04x: can't remove group - group is not set", mtlk_vap_get_oid(nic->vap_handle));
      return;
    }
    break;

  default:
    ELOG_V("Invalid action");
    return;
  }

  /* update fw sta group */
  mtlk_core_mcast_notify_fw(nic->vap_handle, action, mtlk_sta_get_sid(sta), grp_id);
  return;

finish:
  mtlk_osal_lock_release(&nic->mcast.lock);
  return;
}

static void
_mtlk_mc_update_sta_status (mtlk_core_t *nic, mtlk_core_ui_mc_update_sta_db_t *req, mcast_group_info_t *group_info)
{
  /* Depending from "action: STA DB will be updated according the following rules
   * UPDATE:
   *  1) mark all STA in group as candidates for "remove from group" (set status to MCAST_STA_STATUS_DELETE);
   *  2) walk through MAC's list in group. For each MAC try to find corresponding STA in group:
   *      - if STA was found, changes status to "no update" (MCAST_STA_STATUS_NONE);
   *      - if STA wasn't found, add a new STA entry into group and change status to "join to group" (MCAST_STA_STATUS_JOIN);
   *  3) walk through all STA in group and update memebership of STA in group in FW according to the new status;
   *  4) walk through all STA in group and remove all STA's which were marked as MCAST_STA_STATUS_DELETE, others STA's mark as MCAST_STA_STATUS_NONE;
   *  (steps 3 and 4 can be combined).
   *
   * DELETE:
   *  1) walk through MAC's list in group. For each MAC try to find corresponding STA in group:
   *      - if STA was found, changes status to "remove from group" (set status to MCAST_STA_STATUS_DELETE);
   *  2) walk through all STA in group and update memebership of STA in group in FW according to the new status;
   *  3) walk through all STA in group and remove all STA's which were marked as MCAST_STA_STATUS_DELETE, others STA's mark as MCAST_STA_STATUS_NONE;
   *
   * ADD:
   *  1) walk through MAC's list in group. For each MAC try to find corresponding STA in group:
   *      - if STA wasn't found, add a new STA entry into group and change status to "join to group" (MCAST_STA_STATUS_JOIN);
   *  3) walk through all STA in group and update memebership of STA in group in FW according to the new status;
   *  4) walk through all STA in group and mark them as MCAST_STA_STATUS_NONE;
   */

  mcast_sta_t *mcsta, *prev_mcsta, *del_mcsta, *mcsta_root;
  IEEE_ADDR  *mac_list = req->macs_list;
  unsigned i;

  /* STEP 1: if action is UPDATE, walk trough all STA entries and mark all of them as candidate to deletion: */
  if (MTLK_MC_STADB_UPD == req->action) {
    mcsta = group_info->mcsta;
    while (mcsta) {
      mcsta->status = MCAST_STA_STATUS_DELETE;
      mcsta = mcsta->next;
    }
  }

  /* Store original root of stations list */
  mcsta_root = group_info->mcsta;

  /* STEP 2: go trough MAC's list and update status of STA's, associated with source:group */
  for (i = 0; i < req->macs_num; i++, mac_list++) {
    /* Find STA in STADB by MAC address */
    sta_entry *sta = mtlk_stadb_find_sta(&nic->slow_ctx->stadb, mac_list->au8Addr);
    if (!sta) {
      sta = mtlk_hstdb_find_sta(&nic->slow_ctx->hstdb, mac_list->au8Addr);
      if (!sta) {
        /* MAC address does not belong to any known STA */
        ILOG2_DY("CID-%04x: can't find sta:%Y", mtlk_vap_get_oid(nic->vap_handle), mac_list->au8Addr);
        continue;
      }
    }

    /* Find STA in source's entry starting from the original root (w/o new stations) */
    mcsta = __mtlk_mc_find_mcsta(mcsta_root, sta);
    if (MTLK_MC_STADB_DEL == req->action) {
      if (mcsta)
         mcsta->status = MCAST_STA_STATUS_DELETE;
    } else {
      if (mcsta)
        mcsta->status = MCAST_STA_STATUS_NONE;
      else {
        /* Otherwise create a new STA entry at the top of list. */
        mcsta = __mtlk_mc_add_mcsta(&group_info->mcsta, sta);
        if (mcsta)
          mcsta->status = MCAST_STA_STATUS_JOIN;
        else {
          ELOG_DYD("CID-%04x: can't add sta:%Y to group %u: no memory", mtlk_vap_get_oid(nic->vap_handle), mac_list->au8Addr, req->grp_id);
        }
      }
    }

    _mtlk_mc_add_sta_to_decref_list(nic->mcast.decref_sta_list, sta);
  }

  /* STEP 3: Now go through the list of stations and update it according to status of each entry */
  mcsta = group_info->mcsta;
  del_mcsta = prev_mcsta = NULL;
  while (mcsta) {
    mcast_sta_status_t status = mcsta->status;
    mcsta->status = MCAST_STA_STATUS_NONE; /* cleanup status of STA before action */

    switch (status) {
      case MCAST_STA_STATUS_JOIN:
        /* Join to new group in FW */
        mtlk_core_mcast_group_id_action_serialized(nic, MTLK_MC_STA_JOIN_GROUP, req->grp_id, (uint8 *) mtlk_sta_get_addr(mcsta->sta)->au8Addr, &req->mc_addr);
        /* Move to the next entry */
        prev_mcsta = mcsta;
        mcsta = mcsta->next;
        break;

      case MCAST_STA_STATUS_DELETE:
        /* Leave group in FW */
        mtlk_core_mcast_group_id_action_serialized(nic, MTLK_MC_STA_LEAVE_GROUP, group_info->grp_id, (uint8 *) mtlk_sta_get_addr(mcsta->sta)->au8Addr, &req->mc_addr);

        /* Remove entry from the list */
        del_mcsta = mcsta;
        mcsta = mcsta->next;
        if (prev_mcsta)
          prev_mcsta->next = mcsta;
        else
          group_info->mcsta = mcsta;

        /* Release entry */
        _mtlk_mc_add_sta_to_decref_list(nic->mcast.decref_sta_list, del_mcsta->sta);
        mtlk_osal_mem_free(del_mcsta);
        break;

      default:
        /* Move to the next entry */
        prev_mcsta = mcsta;
        mcsta = mcsta->next;
        break;
    }
  }

  /* STEP 4: Update group ID */
  group_info->grp_id = req->grp_id;
}

void __MTLK_IFUNC
mtlk_mc_update_stadb (mtlk_core_t *nic, mtlk_core_ui_mc_update_sta_db_t *req)
{
  mtlk_slist_t *decref_sta_list;

  MTLK_ASSERT(nic);
  MTLK_ASSERT(req);

  /* Do not update STA db, if multucast helper is not available.
   * In this case the group id was already assigned by wlan driver when parsing the IGMP packet */
  if (!mtlk_core_mcast_module_is_available(nic))
    return;

  mtlk_osal_lock_acquire(&nic->mcast.lock);

  __mc_decref_sta_list_init(&nic->mcast);

  switch (req->mc_addr.type) {
    case MTLK_IPv4: {
      /* =========== IPv4 multicast ============ */
      mcast_ip4_grp_t *grp;
      mcast_ip4_src_t *src;

      /* STEP 1: Get/create entry for multicast group address.
       * if entry is not exists and (sta_num == 0 or action is DELETE) --> do nothing */
      grp = _mtlk_mc_find_ip4_grp(&nic->mcast, &req->mc_addr.grp_ip.ip4_addr);
      if (!grp) {
        if ((MTLK_MC_STADB_DEL == req->action) || (0 == req->macs_num)) goto end;
        grp = _mtlk_mc_create_ip4_grp(&nic->mcast, &req->mc_addr.grp_ip.ip4_addr);
        if (!grp) {
          ILOG1_D("Cannot create group IPv4:%B", htonl(req->mc_addr.grp_ip.ip4_addr.s_addr));
          goto end;
        }
        ILOG1_D("Created group IPv4:%B", htonl(grp->ip4_grp_addr.s_addr));
      } else {
        ILOG1_D("Found group IPv4:%B",   htonl(grp->ip4_grp_addr.s_addr));
      }

      /* STEP 2: Get/create mcast source entry.
       * if entry is not exists and (sta_num == 0 or action is DELETE) --> do nothing */
      src = _mtlk_mc_find_ip4_src(grp, &req->mc_addr.src_ip.ip4_addr);
      if (!src) {
        if ((MTLK_MC_STADB_DEL == req->action) || (0 == req->macs_num)) goto end;
        src = _mtlk_mc_create_ip4_src(grp, &req->mc_addr.src_ip.ip4_addr);
        if (!src) goto end;
        ILOG1_D("Created source IPv4:%B", htonl(src->ip4_src_addr.s_addr));

        /* Update group index in HW table. */
        src->info.grp_id = mtlk_core_mcast_handle_grid(nic, &req->mc_addr, MTLK_GRID_ADD, req->grp_id);
        if (MCAST_GROUP_UNDEF == src->info.grp_id) goto end;
      } else {
        ILOG1_D("Found source IPv4:%B",   htonl(src->ip4_src_addr.s_addr));
      }

      /* STEP 3: Update status of stations */
      _mtlk_mc_update_sta_status(nic, req, &src->info);

      /* STEP 4: if last STA was removed, release group index and remove source from group */
      if (!src->info.mcsta) {
        mtlk_core_mcast_handle_grid(nic, &req->mc_addr, MTLK_GRID_DEL, src->info.grp_id);
        if (_mtlk_mc_del_ip4_src(src)) {
          ILOG1_V(" - no more STA's associated with source. Source deleted");
          src = NULL;
        }
      }

      /* STEP 5: delete group, if last source was removed from group */
      if ((!grp->info.mcsta) && (!grp->ip4_src_list)) {
        if (_mtlk_mc_del_ip4_grp(&nic->mcast, grp)) {
          ILOG1_V(" - no more sources in group. Group deleted");
          grp = NULL;
        }
      }
      _mtlk_mc_print_ip4_group(grp);
      break;
    }

    case MTLK_IPv6: {
      /* =========== IPv6 multicast ============ */
      mcast_ip6_grp_t *grp;
      mcast_ip6_src_t *src;

      /* STEP 1: Get/create entry for multicast group address.
       * if entry is not exists and (sta_num == 0 or action is DELETE --> do nothing */
      grp = _mtlk_mc_find_ip6_grp(&nic->mcast, &req->mc_addr.grp_ip.ip6_addr);
      if (!grp) {
        if ((MTLK_MC_STADB_DEL == req->action) || (0 == req->macs_num)) goto end;
        grp = _mtlk_mc_create_ip6_grp(&nic->mcast, &req->mc_addr.grp_ip.ip6_addr);
        if (!grp) {
          ILOG1_K("Cannot create group IPv6:%K", req->mc_addr.grp_ip.ip6_addr.s6_addr);
          goto end;
        }
        ILOG1_K("Created group IPv6:%K", grp->ip6_grp_addr.s6_addr);
      } else {
        ILOG1_K("Found group IPv6:%K",   grp->ip6_grp_addr.s6_addr);
      }

      /* STEP 2: Get/create mcast source entry.
       * if entry is not exists and (sta_num == 0 or action is DELETE --> do nothing */
      src = _mtlk_mc_find_ip6_src(grp, &req->mc_addr.src_ip.ip6_addr);
      if (!src) {
        if ((MTLK_MC_STADB_DEL == req->action) || (0 == req->macs_num)) goto end;
        src = _mtlk_mc_create_ip6_src(grp, &req->mc_addr.src_ip.ip6_addr);
        if (!src) goto end;
        ILOG1_K("Created source IPv6:%K", src->ip6_src_addr.s6_addr);

        /* Update group index in HW table. */
        src->info.grp_id = mtlk_core_mcast_handle_grid(nic, &req->mc_addr, MTLK_GRID_ADD, req->grp_id);
        if (MCAST_GROUP_UNDEF == src->info.grp_id) goto end;
      } else {
        ILOG1_K("Found source IPv6:%K",   src->ip6_src_addr.s6_addr);
      }

      /* STEP 3: Update status of stations */
      _mtlk_mc_update_sta_status(nic, req, &src->info);

      /* STEP 4: if last STA was removed, release group index and remove source from group */
      if (!src->info.mcsta) {
        mtlk_core_mcast_handle_grid(nic, &req->mc_addr, MTLK_GRID_DEL, src->info.grp_id);
        if (_mtlk_mc_del_ip6_src(src)) {
          ILOG1_V(" - no more STA's associated with source. Source deleted");
          src = NULL;
        }
      }

      /* STEP 5: delete group, if last source was removed from group */
      if ((!grp->info.mcsta) && (!grp->ip6_src_list)) {
        if (_mtlk_mc_del_ip6_grp(&nic->mcast, grp)) {
          ILOG1_V(" - no more sources in group. Group deleted");
          grp = NULL;
        }
      }
      _mtlk_mc_print_ip6_group(grp);
      break;
    }

    default:
      break;
  }

end:
  decref_sta_list = __mc_decref_sta_list_release(&nic->mcast);
  mtlk_osal_lock_release(&nic->mcast.lock);
  _mtlk_mc_decref_stas_from_list(decref_sta_list);
  return;
}

int __MTLK_IFUNC
mtlk_mc_get_grid (mtlk_core_t *nic, mtlk_nbuf_t *nbuf)
{
  int group_id = MCAST_GROUP_UNDEF;
  struct ethhdr *ether_header = (struct ethhdr *)nbuf->data;
  mcast_group_info_t  *group;
  mtlk_dlist_entry_t *entry, *head;

  switch (ntohs(ether_header->h_proto)) {
    case ETH_P_IP:
    {
      struct in_addr grp_addr, src_addr;
      struct iphdr *ip_header = (struct iphdr *)
          ((mtlk_handle_t)ether_header + sizeof(struct ethhdr));

      if (mtlk_df_nbuf_get_data_length(nbuf) < sizeof(*ether_header) + sizeof(*ip_header)) {
        ILOG1_D("CID-%04x: Frame length is wrong", mtlk_vap_get_oid(nic->vap_handle));
        return MCAST_GROUP_UNDEF;
      }
      grp_addr.s_addr = ntohl(ip_header->daddr);
      src_addr.s_addr = ntohl(ip_header->saddr);

      /* Check link local multicast space 224.0.0.0/24 */
      if ((grp_addr.s_addr & 0xFFFFFF00) == 0xE0000000) {
        group_id = MCAST_GROUP_BROADCAST;
        ILOG2_D("IPv4 %B: BROADCAST group", htonl(grp_addr.s_addr));
        break;
      }

      mtlk_osal_lock_acquire(&nic->mcast.lock);
      if ((group = _mtlk_mc_find_ip4_group(nic, &grp_addr, &src_addr)) != NULL) {
        /* Group was found */
        group_id = group->grp_id;
      }
      mtlk_osal_lock_release(&nic->mcast.lock);

      if (MCAST_GROUP_UNDEF == group_id) {
        /* Check for all multicast ranges */
        mtlk_osal_lock_acquire(&nic->mcast.lock_ranges);
        mtlk_dlist_foreach(&nic->mcast.ip4_ranges_list, entry, head) {
          mcast_ip4_range *mc_range = MTLK_LIST_GET_CONTAINING_RECORD(entry, mcast_ip4_range, list_entry);
          if (!mtlk_osal_ipv4_masked_addr_cmp(&mc_range->ip4_addr, &mc_range->ip4_mask, &grp_addr)) {
            /* return broadcast group */
            group_id = MCAST_GROUP_BROADCAST;
            break;
          }
        }
        mtlk_osal_lock_release(&nic->mcast.lock_ranges);
      }

      if (MCAST_GROUP_UNDEF == group_id) {
        /* Drop unknown multicast */
        mtlk_core_inc_cnt(nic, MTLK_CORE_CNT_TX_PACKETS_DISCARDED_DRV_UNKNOWN_DESTINATION_MCAST);
      }

      ILOG2_DDD("IPv4 %B --> %B: return group %d", htonl(src_addr.s_addr), htonl(grp_addr.s_addr), group_id);
      break;
    }

    case ETH_P_IPV6:
    {
      struct in6_addr grp_addr, src_addr;
      struct ipv6hdr *ip_header = (struct ipv6hdr *)
          ((mtlk_handle_t)ether_header + sizeof(struct ethhdr));
      if (skb_headlen(nbuf) < sizeof(*ether_header) + sizeof(*ip_header)) {
        ILOG1_D("CID-%04x: Frame length is wrong", mtlk_vap_get_oid(nic->vap_handle));
        return MCAST_GROUP_UNDEF;
      }

      grp_addr.in6_u.u6_addr32[0] = ntohl(ip_header->daddr.in6_u.u6_addr32[0]);
      grp_addr.in6_u.u6_addr32[1] = ntohl(ip_header->daddr.in6_u.u6_addr32[1]);
      grp_addr.in6_u.u6_addr32[2] = ntohl(ip_header->daddr.in6_u.u6_addr32[2]);
      grp_addr.in6_u.u6_addr32[3] = ntohl(ip_header->daddr.in6_u.u6_addr32[3]);

      src_addr.in6_u.u6_addr32[0] = ntohl(ip_header->saddr.in6_u.u6_addr32[0]);
      src_addr.in6_u.u6_addr32[1] = ntohl(ip_header->saddr.in6_u.u6_addr32[1]);
      src_addr.in6_u.u6_addr32[2] = ntohl(ip_header->saddr.in6_u.u6_addr32[2]);
      src_addr.in6_u.u6_addr32[3] = ntohl(ip_header->saddr.in6_u.u6_addr32[3]);

      /* Check link local multicast space FF02::/16 */
      if ((grp_addr.in6_u.u6_addr32[0] & 0xFF0F0000) == 0xFF020000) {
        group_id = MCAST_GROUP_BROADCAST;
        ILOG2_K("IPv6 %K: BROADCAST group", grp_addr.s6_addr);
        break;
      }

      mtlk_osal_lock_acquire(&nic->mcast.lock);
      if ((group = _mtlk_mc_find_ip6_group(nic, &src_addr, &grp_addr)) != NULL) {
        /* Group was found */
        group_id = group->grp_id;
      }
      mtlk_osal_lock_release(&nic->mcast.lock);

      if (MCAST_GROUP_UNDEF == group_id) {
        /* Check for all multicast ranges */
        mtlk_osal_lock_acquire(&nic->mcast.lock_ranges);
        mtlk_dlist_foreach(&nic->mcast.ip6_ranges_list, entry, head) {
          mcast_ip6_range *mc_range = MTLK_LIST_GET_CONTAINING_RECORD(entry, mcast_ip6_range, list_entry);
          if (!mtlk_osal_ipv6_masked_addr_cmp(&mc_range->ip6_addr, &mc_range->ip6_mask, &ip_header->daddr)) {
            /* return broadcast group */
            group_id = MCAST_GROUP_BROADCAST;
            break;
          }
        }
        mtlk_osal_lock_release(&nic->mcast.lock_ranges);
      }

      if (MCAST_GROUP_UNDEF == group_id) {
        /* Drop unknown multicast */
        mtlk_core_inc_cnt(nic, MTLK_CORE_CNT_TX_PACKETS_DISCARDED_DRV_UNKNOWN_DESTINATION_MCAST);
      }

      ILOG2_KKD("IPv6: %K --> %K: return group %d", src_addr.s6_addr, grp_addr.s6_addr, group_id);
      break;
    }

  case MTLK_ETH_P_1905_1:
    {
      group_id = MCAST_GROUP_BROADCAST;
      ILOG2_Y("IEEE 1905.1 %Y: BROADCAST group", ether_header->h_source);
      break;
    }

  default:
    ILOG4_DD("CID-%04x: Not IP protocol (%04x), but multicast flags set", ntohs(ether_header->h_proto), mtlk_vap_get_oid(nic->vap_handle));
    mtlk_dump(4, nbuf->data, nbuf->len, "Packet dump");
  }

  return group_id;
}

/***********************************************************
 * Multicast range management
 ***********************************************************/

static mtlk_dlist_entry_t*
_mtlk_mc_find_range_ip4 (mcast_ctx_t *mcast, struct in_addr *addr, struct in_addr *mask)
{
  mtlk_dlist_entry_t *entry, *head;
  mcast_ip4_range    *mc_range = NULL;

  mtlk_dlist_foreach(&mcast->ip4_ranges_list, entry, head) {
    mc_range = MTLK_LIST_GET_CONTAINING_RECORD(entry, mcast_ip4_range, list_entry);
    if (!mtlk_osal_ipv4_addr_cmp(&mc_range->ip4_addr, addr) &&
        !mtlk_osal_ipv4_addr_cmp(&mc_range->ip4_mask, mask)) {
        return entry;
    }
  }
  return NULL;
}

static mtlk_dlist_entry_t*
_mtlk_mc_find_range_ip6 (mcast_ctx_t *mcast, struct in6_addr *addr, struct in6_addr *mask)
{
  mtlk_dlist_entry_t *entry, *head;
  mcast_ip6_range    *mc_range = NULL;

  ILOG5_KK("find: %K/%K", addr->s6_addr, mask->s6_addr);
  mtlk_dlist_foreach(&mcast->ip6_ranges_list, entry, head) {
    mc_range = MTLK_LIST_GET_CONTAINING_RECORD(entry, mcast_ip6_range, list_entry);
    ILOG5_KK("entry: %K/%K", mc_range->ip6_addr.s6_addr, mc_range->ip6_mask.s6_addr);
    if (!mtlk_osal_ipv6_addr_cmp(&mc_range->ip6_addr, addr) &&
        !mtlk_osal_ipv6_addr_cmp(&mc_range->ip6_mask, mask)) {
        ILOG5_V("found");
        return entry;
    }
  }
  return NULL;
}

#define MTLK_MC_FREE_RANGE_IP4(nic, entry)  { \
  mcast_ip4_range *mc_range = MTLK_LIST_GET_CONTAINING_RECORD(entry, mcast_ip4_range, list_entry); \
  ILOG2_DDD("CID-%04x: delete mcast range %B/%B", mtlk_vap_get_oid(nic->vap_handle),               \
      htonl(mc_range->ip4_addr.s_addr), htonl(mc_range->ip4_mask.s_addr));                         \
  mtlk_osal_mem_free(mc_range); }

#define MTLK_MC_FREE_RANGE_IP6(nic, entry)  { \
  mcast_ip6_range *mc_range = MTLK_LIST_GET_CONTAINING_RECORD(entry, mcast_ip6_range, list_entry); \
  ILOG2_DKK("CID-%04x: delete mcast range %K/%K", mtlk_vap_get_oid(nic->vap_handle),               \
      mc_range->ip6_addr.s6_addr, mc_range->ip6_mask.s6_addr);                                     \
  mtlk_osal_mem_free(mc_range); }

int  __MTLK_IFUNC
mtlk_mc_ranges_add (mtlk_core_t *nic, mtlk_ip_netmask_t *netmask)
{
  MTLK_ASSERT(netmask);
  switch (netmask->type) {
    case MTLK_IPv4: {
      mcast_ip4_range *mc_range;

      /* Check if entry already exists */
      if (_mtlk_mc_find_range_ip4(&nic->mcast, &netmask->addr.ip4_addr, &netmask->mask.ip4_addr)) {
        ILOG0_V("Entry already exists");
        return MTLK_ERR_OK;
      }

      /* Allocate a new entry */
      if (NULL == (mc_range = mtlk_osal_mem_alloc(sizeof(mcast_ip4_range), MTLK_MEM_TAG_MCAST_RANGE))) {
        ELOG_V("Can't allocate mcast range entry");
        return MTLK_ERR_NO_MEM;
      }
      /* Setup entry and add to list */
      mc_range->ip4_addr = netmask->addr.ip4_addr;
      mc_range->ip4_mask = netmask->mask.ip4_addr;

      mtlk_osal_lock_acquire(&nic->mcast.lock_ranges);
      mtlk_dlist_push_back(&nic->mcast.ip4_ranges_list, &mc_range->list_entry);
      mtlk_osal_lock_release(&nic->mcast.lock_ranges);

      ILOG2_DDD("CID-%04x: added mcast range %B/%B", mtlk_vap_get_oid(nic->vap_handle),
          htonl(netmask->addr.ip4_addr.s_addr), htonl(netmask->mask.ip4_addr.s_addr));
      return MTLK_ERR_OK;
    }

    case MTLK_IPv6: {
      mcast_ip6_range *mc_range;

      /* Check if entry already exists */
      if (_mtlk_mc_find_range_ip6(&nic->mcast, &netmask->addr.ip6_addr, &netmask->mask.ip6_addr)) {
        ILOG0_V("Entry already exists");
        return MTLK_ERR_OK;
      }

      /* Allocate a new entry */
      if (NULL == (mc_range = mtlk_osal_mem_alloc(sizeof(mcast_ip6_range), MTLK_MEM_TAG_MCAST_RANGE))) {
        ELOG_V("Can't allocate mcast range entry");
        return MTLK_ERR_NO_MEM;
      }
      /* Setup entry and add to list */
      mc_range->ip6_addr = netmask->addr.ip6_addr;
      mc_range->ip6_mask = netmask->mask.ip6_addr;

      mtlk_osal_lock_acquire(&nic->mcast.lock_ranges);
      mtlk_dlist_push_back(&nic->mcast.ip6_ranges_list, &mc_range->list_entry);
      mtlk_osal_lock_release(&nic->mcast.lock_ranges);

      ILOG2_DKK("CID-%04x: added mcast range %K/%K", mtlk_vap_get_oid(nic->vap_handle),
          netmask->addr.ip6_addr.s6_addr, netmask->mask.ip6_addr.s6_addr);
      return MTLK_ERR_OK;
    }

    default:
      return MTLK_ERR_VALUE;
  }
}

int  __MTLK_IFUNC
mtlk_mc_ranges_del (mtlk_core_t *nic, mtlk_ip_netmask_t *netmask)
{
  mtlk_dlist_entry_t *entry;
  int res = MTLK_ERR_OK;

  MTLK_ASSERT(netmask);
  mtlk_osal_lock_acquire(&nic->mcast.lock_ranges);

  switch (netmask->type) {
    case MTLK_IPv4: {
      entry = _mtlk_mc_find_range_ip4(&nic->mcast, &netmask->addr.ip4_addr, &netmask->mask.ip4_addr);
      if (entry) {
        mtlk_dlist_remove(&nic->mcast.ip4_ranges_list, entry);
        MTLK_MC_FREE_RANGE_IP4(nic, entry);
      } else {
        ILOG0_V("Entry doesn't exist");
      }
      break;
    }

    case MTLK_IPv6: {
      entry = _mtlk_mc_find_range_ip6(&nic->mcast, &netmask->addr.ip6_addr, &netmask->mask.ip6_addr);
      if (entry) {
        mtlk_dlist_remove(&nic->mcast.ip6_ranges_list, entry);
        MTLK_MC_FREE_RANGE_IP6(nic, entry);
      } else {
        ILOG0_V("Entry doesn't exist");
      }
      break;
    }

    default:
      res = MTLK_ERR_VALUE;
      break;
  }

  mtlk_osal_lock_release(&nic->mcast.lock_ranges);
  return res;
}

int __MTLK_IFUNC
mtlk_mc_ranges_cleanup (mtlk_core_t *nic, ip_type_t type)
{
  mtlk_dlist_entry_t *entry;
  int res = MTLK_ERR_OK;

  mtlk_osal_lock_acquire(&nic->mcast.lock_ranges);

  switch (type) {
    case MTLK_IPv4: {
      /* Cleanup IPv4 ranges */
      while ((entry = mtlk_dlist_pop_front(&nic->mcast.ip4_ranges_list)))
      {
        MTLK_MC_FREE_RANGE_IP4(nic, entry);
      }
      break;
    }

    case MTLK_IPv6: {
      /* Cleanup IPv6 ranges */
      while ((entry = mtlk_dlist_pop_front(&nic->mcast.ip6_ranges_list)))
      {
        MTLK_MC_FREE_RANGE_IP6(nic, entry);
      }
      break;
    }

    default:
      res = MTLK_ERR_VALUE;
      break;
  }

  mtlk_osal_lock_release(&nic->mcast.lock_ranges);
  return res;
}

static const struct in6_addr ssdp_addr_ip6 = {{{0xFF,0x00,0x00,0x00,  0x00,0x00,0x00,0x00,  0x00,0x00,0x00,0x00,  0x00,0x00,0x00,0x0C}}};
static const struct in6_addr ssdp_mask_ip6 = {{{0xFF,0xF0,0xFF,0xFF,  0xFF,0xFF,0xFF,0xFF,  0xFF,0xFF,0xFF,0xFF,  0xFF,0xFF,0xFF,0xFF}}};

static int
_mtlk_mc_ranges_init_all (mtlk_core_t *nic)
{
  int res;
  mtlk_ip_netmask_t netmask;

  /* IPv4: add SSDP protocol 239.255.255.250/255.255.255.255 */
  netmask.type = MTLK_IPv4;
  netmask.addr.ip4_addr.s_addr = 0xEFFFFFFA;
  netmask.mask.ip4_addr.s_addr = 0xFFFFFFFF;
  res = mtlk_mc_ranges_add (nic, &netmask);
  if (MTLK_ERR_OK != res) {
    return res;
  }

  /* IPv6: add SSDP protocol ff0X::c --> FF00:0000:0000:0000:0000:0000:0000:000C/FFF0:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF */
  netmask.type = MTLK_IPv6;
  netmask.addr.ip6_addr = ssdp_addr_ip6;
  netmask.mask.ip6_addr = ssdp_mask_ip6;
  res = mtlk_mc_ranges_add (nic, &netmask);
  return res;
}

static void
_mtlk_mc_ranges_cleanup_all (mtlk_core_t *nic)
{
  mtlk_dlist_entry_t *entry;
  mtlk_osal_lock_acquire(&nic->mcast.lock_ranges);

  /* Cleanup IPv4 ranges */
  while ((entry = mtlk_dlist_pop_front(&nic->mcast.ip4_ranges_list)))
  {
    MTLK_MC_FREE_RANGE_IP4(nic, entry);
  }

  /* Cleanup IPv6 ranges */
  while ((entry = mtlk_dlist_pop_front(&nic->mcast.ip6_ranges_list)))
  {
    MTLK_MC_FREE_RANGE_IP6(nic, entry);
  }

  mtlk_osal_lock_release(&nic->mcast.lock_ranges);
}

int __MTLK_IFUNC
mtlk_mc_ranges_get_vect (mtlk_core_t *nic, ip_type_t ip_type, mtlk_mcast_range_vect_cfg_t *mcast_range_vect_cfg)
{
  int res = MTLK_ERR_OK;
  mtlk_dlist_entry_t *entry, *head;
  mtlk_ip_netmask_t   netmask;

  mtlk_clpb_t *clpb_range_vect = mtlk_clpb_create();
  if (!clpb_range_vect) {
    ELOG_V("Cannot allocate clipboard object");
    res = MTLK_ERR_NO_MEM;
    goto end;
  }

  netmask.type = ip_type;
  mtlk_osal_lock_acquire(&nic->mcast.lock_ranges);

  switch (ip_type) {
    case MTLK_IPv4:
      mtlk_dlist_foreach(&nic->mcast.ip4_ranges_list, entry, head) {
        mcast_ip4_range *mc_range = MTLK_LIST_GET_CONTAINING_RECORD(entry, mcast_ip4_range, list_entry);
        netmask.addr.ip4_addr = mc_range->ip4_addr;
        netmask.mask.ip4_addr = mc_range->ip4_mask;
        ILOG2_DDD("CID-%04x: ranges list entry %B/%B", mtlk_vap_get_oid(nic->vap_handle),
            htonl(netmask.addr.ip4_addr.s_addr), htonl(netmask.mask.ip4_addr.s_addr));
        res = mtlk_clpb_push(clpb_range_vect, &netmask, sizeof(netmask));
        if (MTLK_ERR_OK != res) {
          ELOG_V("Cannot push data to the clipboard");
          goto err_push_data;
        }
      }
      goto no_err;

    case MTLK_IPv6:
      mtlk_dlist_foreach(&nic->mcast.ip6_ranges_list, entry, head) {
        mcast_ip6_range *mc_range = MTLK_LIST_GET_CONTAINING_RECORD(entry, mcast_ip6_range, list_entry);
        netmask.addr.ip6_addr = mc_range->ip6_addr;
        netmask.mask.ip6_addr = mc_range->ip6_mask;
        ILOG2_DKK("CID-%04x: ranges list entry %K/%K", mtlk_vap_get_oid(nic->vap_handle),
            netmask.addr.ip6_addr.s6_addr, netmask.mask.ip6_addr.s6_addr);
        res = mtlk_clpb_push(clpb_range_vect, &netmask, sizeof(netmask));
        if (MTLK_ERR_OK != res) {
          ELOG_V("Cannot push data to the clipboard");
          goto err_push_data;
        }
      }
      goto no_err;

    default:
      res = MTLK_ERR_VALUE;
  }

err_push_data:
  mtlk_clpb_delete(clpb_range_vect);
  clpb_range_vect = NULL;

no_err:
  mtlk_osal_lock_release(&nic->mcast.lock_ranges);

end:
  mcast_range_vect_cfg->range_vect = clpb_range_vect;
  return res;
}
