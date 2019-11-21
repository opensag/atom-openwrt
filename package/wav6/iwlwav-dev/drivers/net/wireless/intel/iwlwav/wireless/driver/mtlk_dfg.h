/******************************************************************************

                               Copyright (c) 2012
                            Lantiq Deutschland GmbH

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/
/*
 * $Id$
 *
 * Driver framework global API.
 *
 */

#ifndef __DFG_H__
#define __DFG_H__

/*! \fn      void mtlk_dfg_init(void)
    
    \brief   inits global df object.

    \param   dfg         df object

    \return  mtlk_err... code
*/
int mtlk_dfg_init(void);

/*! \fn      void mtlk_dfg_cleanup(void)
    
    \brief   cleanups global df object.

    \param   dfg         df object
*/
void mtlk_dfg_cleanup(void);

/*! \fn
    \brief   Returns pointer to driver-level IRB object
*/
mtlk_irbd_t * __MTLK_IFUNC
mtlk_dfg_get_driver_irbd(void);

/*! \fn
    \brief   Returns pointer to driver-level WSS object
*/
mtlk_wss_t * __MTLK_IFUNC
mtlk_dfg_get_driver_wss(void);

/*! \fn
    \brief   Returns pointer to driver-level STA database object
*/
global_stadb_t * __MTLK_IFUNC
mtlk_dfg_get_driver_stadb(void);


#endif /* __DFG_H__ */
