#ifndef EVRMA_SIM_H_
#define EVRMA_SIM_H_

#include "evr-internal.h"
#include "mng-dev.h"

int evr_sim_init(struct evr_hw_data *evr_hw_data);
void evr_sim_end(struct evr_hw_data *evr_hw_data);
int evr_sim_set_output_to_pulsegen(struct evr_hw_data *evr_hw_data, 
		int index_output, int index_pulsegen);

int evr_sim_set_output_to_misc_func(struct evr_hw_data *evr_hw_data, 
		int index_output, int misc_func);

int evr_sim_get_pulsegen_param(struct evr_hw_data *evr_hw_data,
					int index,
					u32 *prescaler,
					u32 *delay,
					u32 *width);

int evr_sim_set_pulsegen_param(struct evr_hw_data *evr_hw_data,
					int index,
					u32 prescaler,
					u32 delay,
					u32 width);

int evr_sim_init_res(struct evr_hw_data *evr_hw_data,
				int res_type, int res_index);

ssize_t evr_sim_dbg(struct evr_hw_data *evr_hw_data, 
						const char *buf, size_t count);

ssize_t evr_sim_dbg_res(struct evr_hw_data *evr_hw_data, 
						char *buf, size_t count, int res_type,
						int res_index);

void evr_sim_irq_set(struct modac_mngdev_des *mngdev_des, int enabled);

extern struct modac_io_rw_plugin		evr_sim_rw_plugin;

#endif /* EVRMA_SIM_H_ */
