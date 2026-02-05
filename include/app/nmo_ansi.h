/**
 * @file nmo_ansi.h
 * @brief ANSI terminal styling helpers (escape codes)
 */

#ifndef NMO_ANSI_H
#define NMO_ANSI_H

/* Basic ANSI escape codes */
#define NMO_ANSI_RESET   "\033[0m"
#define NMO_ANSI_BOLD    "\033[1m"
#define NMO_ANSI_DIM     "\033[2m"

#define NMO_ANSI_RED     "\033[31m"
#define NMO_ANSI_GREEN   "\033[32m"
#define NMO_ANSI_YELLOW  "\033[33m"
#define NMO_ANSI_BLUE    "\033[34m"
#define NMO_ANSI_MAGENTA "\033[35m"
#define NMO_ANSI_CYAN    "\033[36m"
#define NMO_ANSI_WHITE   "\033[37m"

#endif /* NMO_ANSI_H */
