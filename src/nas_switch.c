/*
 * Copyright (c) 2016 Dell Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License. You may obtain
 * a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * THIS CODE IS PROVIDED ON AN *AS IS* BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT
 * LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS
 * FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.
 *
 * See the Apache Version 2.0 License for specific language governing
 * permissions and limitations under the License.
 */

/*
 * nas_switch.c
 *
 *  Created on: Mar 31, 2015
 */


#include "nas_switch.h"
#include "std_assert.h"
#include "std_config_node.h"
#include "std_envvar.h"
#include "std_utils.h"
#include "event_log.h"

#include <string.h>
#include <stdio.h>

static const char *switch_cfg_path = NULL;
static bool failed = false;

static nas_switches_t switches;
static size_t num_switches = 0;
static int MAX_DIR_SIZE = 256;

static nas_switch_detail_t *switch_cfg;

size_t nas_switch_get_max_npus(void) {
    return num_switches;
}

uint32_t * int_array_from_string(const char * str, size_t *expected_len) {
    std_parsed_string_t ps = NULL;
    if (!std_parse_string(&ps,str,",")) {
        return NULL;
    }
    uint32_t * lst = NULL;
    do {
        *expected_len = std_parse_string_num_tokens(ps);
        lst = (uint32_t*)calloc(*expected_len,sizeof(*lst));
        if (lst==NULL) break;
        size_t ix = 0;
        for ( ; ix < *expected_len ; ++ix ) {
            lst[ix] = atoi(std_parse_string_at(ps,ix));
        }
    } while(0);
    std_parse_string_free(ps);
    return lst;
}


void _fn_switch_parser(std_config_node_t node, void *user_data) {
    const char * name = std_config_name_get(node);

    if (failed) return;    //if already failed.. no sense in continuing

    if (strcmp(name,"switch_topology")==0) {
        const char * switch_ids = std_config_attr_get(node,"switch_ids");
        if (switch_ids==NULL) {
            EV_LOGGING(NAS_COM, ERR, "SWITCH","Missing switch_ids - switch file config. Exiting... fix %s",switch_cfg_path);
            return;
        }

        switches.switch_list = int_array_from_string(switch_ids,&switches.number_of_switches);
        if (switches.switch_list==NULL) {
            EV_LOGGING(NAS_COM, ERR, "SWITCH","Switch ids are invalid %s - switch file config. Exiting... fix %s",switch_ids,switch_cfg_path);
            failed = true; return;
        }
        //really no point in cleaning up because the switch init failure will cause the calling process to exit

        switch_cfg = (nas_switch_detail_t *)calloc(switches.number_of_switches,sizeof(*switch_cfg));
        if (switch_cfg==NULL) failed = true;
        if (failed) EV_LOGGING(NAS_COM, ERR, "SWITCH","memory alloc failed switch file config. Exiting...",switch_ids,switch_cfg_path);
    }

    if (strcmp(name,"switch")==0) {
        const char * id = std_config_attr_get(node,"id");
        const char * npus = std_config_attr_get(node,"npus");
        if (id==NULL || npus==NULL) {
            EV_LOGGING(NAS_COM, ERR, "SWITCH","Invalid switch config. Exiting... fix %s",switch_cfg_path);
            return;
        }
        size_t switch_ix =atoi(id);
        if (switch_ix >= switches.number_of_switches) {
            EV_LOGGING(NAS_COM, ERR, "SWITCH","Invalid switch id = %s. Exiting... fix %s",id,switch_cfg_path);
            failed = true;
            return;
        }
        switch_cfg[switch_ix].npus = (npu_id_t*)int_array_from_string(npus,&switch_cfg[switch_ix].number_of_npus);
        if (switch_cfg[switch_ix].npus==NULL) failed = true;

        num_switches += switch_cfg[switch_ix].number_of_npus;

    }

}

t_std_error nas_switch_init(void) {
  char str[MAX_DIR_SIZE];

    switch_cfg_path = std_getenv("DN_SWITCH_CFG");
    if (switch_cfg_path==NULL) {
        if (getenv("OPX_INSTALL_PATH")) {
          snprintf(str, MAX_DIR_SIZE, "%s%s", getenv("OPX_INSTALL_PATH"), NAS_SWITCH_CFG_DEFAULT_PATH);
          switch_cfg_path = str;
        }
        else
            switch_cfg_path = NAS_SWITCH_CFG_DEFAULT_PATH;
    }

    std_config_hdl_t h = std_config_load(switch_cfg_path);
    if (h==NULL) return STD_ERR(HALCOM,FAIL,0);
    t_std_error rc = STD_ERR(HALCOM,FAIL,0);;
    do {
        std_config_node_t node = std_config_get_root(h);
        if (node==NULL) break;

        std_config_for_each_node(node,_fn_switch_parser,NULL);
        rc = STD_ERR_OK;
    } while(0);

    std_config_unload(h);
    if (failed) rc = STD_ERR(HALCOM,FAIL,0);
    return rc;
}

const nas_switches_t * nas_switch_inventory() {
    return &switches;
}

const nas_switch_detail_t *nas_switch(nas_switch_id_t switch_id) {
    if (switch_id >= nas_switch_inventory()->number_of_switches) return NULL;
    return &(switch_cfg[switch_id]);
}

bool nas_find_switch_id_by_npu(uint_t npu_id, nas_switch_id_t * p_switch_id) {
    int switch_id = 0;
    int npu;
    for ( ; switch_id < nas_switch_inventory()->number_of_switches; switch_id++) {
        for (npu = 0; npu < switch_cfg[switch_id].number_of_npus; npu++) {
            if (switch_cfg[switch_id].npus[npu] == npu_id) {
                if (p_switch_id)
                    *p_switch_id = switch_id;
                return true;
            }
        }
    }

    return false;
}
