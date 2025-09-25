#ifndef KICKCAT_DEBUG_H
#define KICKCAT_DEBUG_H

#include "kickcat/Error.h"

namespace kickcat
{

#define _none(...)    do { if (0) { fprintf(stderr, ##__VA_ARGS__); } } while (0);
#define _error(...)   do { fprintf(stderr, "[E] %s ", LOCATION()); fprintf(stderr, ##__VA_ARGS__); } while(0);
#define _warning(...) do { fprintf(stderr, "[W] %s ", LOCATION()); fprintf(stderr, ##__VA_ARGS__); } while(0);
#define _info(...)    do { fprintf(stdout, "[I] %s ", LOCATION()); fprintf(stdout, ##__VA_ARGS__); } while(0);


#ifdef DEBUG_BUS_ERROR
    #define bus_error   _error
#else
    #define bus_error   _none
#endif

#ifdef DEBUG_BUS_WARNING
    #define bus_warning _warning
#else
    #define bus_warning _none
#endif

#ifdef DEBUG_BUS_INFO
    #define bus_info    _info
#else
    #define bus_info    _none
#endif


#ifdef DEBUG_LINK_ERROR
    #define link_error   _error
#else
    #define link_error   _none
#endif

#ifdef DEBUG_LINK_WARNING
    #define link_warning _warning
#else
    #define link_warning _none
#endif

#ifdef DEBUG_LINK_INFO
    #define link_info    _info
#else
    #define link_info    _none
#endif


#ifdef DEBUG_SOCKET_ERROR
    #define socket_error   _error
#else
    #define socket_error   _none
#endif

#ifdef DEBUG_SOCKET_WARNING
    #define socket_warning _warning
#else
    #define socket_warning _none
#endif

#ifdef DEBUG_SOCKET_INFO
    #define socket_info    _info
#else
    #define socket_info    _none
#endif


#ifdef DEBUG_GATEWAY_ERROR
    #define gateway_error   _error
#else
    #define gateway_error   _none
#endif

#ifdef DEBUG_GATEWAY_WARNING
    #define gateway_warning _warning
#else
    #define gateway_warning _none
#endif

#ifdef DEBUG_GATEWAY_INFO
    #define gateway_info    _info
#else
    #define gateway_info    _none
#endif


#ifdef DEBUG_COE_ERROR
    #define coe_error   _error
#else
    #define coe_error   _none
#endif

#ifdef DEBUG_COE_WARNING
    #define coe_warning _warning
#else
    #define coe_warning _none
#endif

#ifdef DEBUG_COE_INFO
    #define coe_info    _info
#else
    #define coe_info    _none
#endif


#ifdef DEBUG_SLAVE_ERROR
    #define slave_error   _error
#else
    #define slave_error   _none
#endif

#ifdef DEBUG_SLAVE_WARNING
    #define slave_warning _warning
#else
    #define slave_warning _none
#endif

#ifdef DEBUG_SLAVE_INFO
    #define slave_info    _info
#else
    #define slave_info    _none
#endif


#ifdef DEBUG_SIMU_ERROR
    #define simu_error   _error
#else
    #define simu_error   _none
#endif

#ifdef DEBUG_SIMU_WARNING
    #define simu_warning _warning
#else
    #define simu_warning _none
#endif

#ifdef DEBUG_SIMU_INFO
    #define simu_info    _info
#else
    #define simu_info    _none
#endif


#ifdef DEBUG_ESI_ERROR
    #define esi_error   _error
#else
    #define esi_error   _none
#endif

#ifdef DEBUG_ESI_WARNING
    #define esi_warning _warning
#else
    #define esi_warning _none
#endif

#ifdef DEBUG_ESI_INFO
    #define esi_info    _info
#else
    #define esi_info    _none
#endif

#ifdef DEBUG_SPI_ERROR
    #define spi_error   _error
#else
    #define spi_error   _none
#endif

#ifdef DEBUG_SPI_WARNING
    #define spi_warning _warning
#else
    #define spi_warning _none
#endif

#ifdef DEBUG_SPI_INFO
    #define spi_info    _info
#else
    #define spi_info    _none
#endif


}

#endif
