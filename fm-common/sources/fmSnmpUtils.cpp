//
// Copyright (c) 2014 Wind River Systems, Inc.
//
// SPDX-License-Identifier: Apache-2.0
//

#include <stdlib.h>
#include <iostream>
#include <sstream>
#include <string>
#include <map>
#include <assert.h>
#include <sstream>

#include "fmDbAPI.h"
#include "fmFile.h"
#include "fmAPI.h"
#include "fmMsg.h"
#include "fmLog.h"
#include "fmDb.h"
#include "fmDbUtils.h"
#include "fmSnmpConstants.h"
#include "fmSnmpUtils.h"

typedef std::map<int,std::string> int_to_objtype;

static int_to_objtype objtype_map;
static pthread_mutex_t mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;


static void add_to_table(int t, std::string objtype, int_to_objtype &tbl) {
	tbl[t]=objtype;
}

static void init_objtype_table() {
	pthread_mutex_lock(&mutex);
	static bool has_inited=false;
	while (!has_inited){
		add_to_table(FM_ALARM_SEVERITY_CLEAR, ALARM_MSG, objtype_map);
		add_to_table(FM_ALARM_SEVERITY_WARNING, ALARM_WARNING, objtype_map);
		add_to_table(FM_ALARM_SEVERITY_MINOR, ALARM_MINOR, objtype_map);
		add_to_table(FM_ALARM_SEVERITY_MAJOR, ALARM_MAJOR, objtype_map);
		add_to_table(FM_ALARM_SEVERITY_CRITICAL, ALARM_CRITICAL, objtype_map);
		add_to_table(FM_ALARM_CLEAR, ALARM_CLEAR, objtype_map);
		add_to_table(FM_ALARM_HIERARCHICAL_CLEAR, ALARM_HIERARCHICAL_CLEAR, objtype_map);
		add_to_table(FM_ALARM_MESSAGE, ALARM_MSG, objtype_map);
		add_to_table(FM_WARM_START, WARM_START, objtype_map);
		has_inited=true;
	}
	pthread_mutex_unlock(&mutex);
}

static std::string add_time_val(std::string &str,
		const std::string &objtype, FMTimeT time){
	std::string time_str;
	fm_db_util_make_timestamp_string(time_str, time, true);
	return str + objtype + STR_TYPE + time_str + SEP;
}

static std::string add_str_val(std::string &str,
		const std::string &objtype, const char *value){
	std::string val(value);
    return str + objtype + STR_TYPE + '"' + val + '"' + SEP;
}

static std::string add_int_val(std::string &str,
		const std::string &objtype, int value){
    return str + objtype + INT_TYPE + fm_db_util_int_to_string(value) + SEP;
}

static std::string get_trap_objtype(int type){
	init_objtype_table();
	return objtype_map[type];
}
static bool get_trap_dest_list(CFmDBSession &sess,fm_db_result_t & res){
	std::string cmd;

	fm_db_util_build_sql_query(FM_TRAPDEST_TABLE_NAME, NULL, cmd);
	return sess.query(cmd.c_str(), res);
}

static std::string format_trap_cmd(CFmDBSession &sess, int type, SFmAlarmDataT &data,
		std::string &ip, std::string &comm){
	std::string cmd;
	std::string objtype;
	std::string mib;
	std::string s = "\"\" ";
        std::string env;

	if (get_trap_objtype(type) == WARM_START)
		mib = SNMPv2_MIB;
	else
		mib = WRS_ALARM_MIB;

	objtype = mib + SCOPE + get_trap_objtype(type);

        if (comm.compare(DC_COMM_STR) == 0){
               env = CONF_PATH_ENV + CONF_DIR + SEP;
        }
        else {
               env = "";
        }

	cmd = env + TRAP_CMD + OPTION_COMM + comm + SEP + ip + SEP + s + objtype + SEP;
	std::string operation_type =get_trap_objtype(type);

	if (operation_type == ALARM_CLEAR){
		cmd = add_str_val(cmd,ALARM_ID, data.alarm_id);
    	cmd = add_str_val(cmd, ALARM_INSTANCE_ID, data.entity_instance_id);
    	cmd = add_time_val(cmd, ALARM_DATE_TIME, data.timestamp);
    	cmd = add_str_val(cmd, ALARM_REASON_TEXT, data.reason_text);
	} else if (operation_type == ALARM_HIERARCHICAL_CLEAR){
		cmd = add_str_val(cmd, ALARM_INSTANCE_ID, data.entity_instance_id);
		cmd = add_time_val(cmd, ALARM_DATE_TIME, 0);
    	cmd = add_str_val(cmd, ALARM_REASON_TEXT, CLEAR_REASON_TEXT.c_str());
	} else if (operation_type == ALARM_MSG){
		cmd = add_str_val(cmd, CUSTOMER_LOG_ID, data.alarm_id);
    	cmd = add_str_val(cmd, CUSTOMER_LOG_INSTANCE_ID, data.entity_instance_id);
    	cmd = add_time_val(cmd, CUSTOMER_LOG_DATE_TIME, data.timestamp);
		cmd = add_int_val(cmd, CUSTOMER_LOG_SEVERITY, data.severity);
    	cmd = add_str_val(cmd, CUSTOMER_LOG_REASON_TEXT, data.reason_text);
		cmd = add_int_val(cmd, CUSTOMER_LOG_EVENT_TYPE, data.alarm_type);
		cmd = add_int_val(cmd, CUSTOMER_LOG_CAUSE, data.probable_cause);
    	cmd = add_int_val(cmd, CUSTOMER_LOG_SERVICE_AFFECTING, data.service_affecting);
	} else if (operation_type == WARM_START){
		// nothing to add to cmd
	} else {
		cmd = add_str_val(cmd, ALARM_ID, data.alarm_id);
		cmd = add_str_val(cmd, ALARM_INSTANCE_ID, data.entity_instance_id);
		cmd = add_time_val(cmd, ALARM_DATE_TIME, data.timestamp);
		cmd = add_int_val(cmd, ALARM_SEVERITY, data.severity);
		cmd = add_str_val(cmd, ALARM_REASON_TEXT, data.reason_text);
		cmd = add_int_val(cmd, ALARM_EVENT_TYPE, data.alarm_type);
		cmd = add_int_val(cmd, ALARM_CAUSE, data.probable_cause);
    	cmd = add_str_val(cmd, ALARM_REPAIR_ACTION, data.proposed_repair_action);
    	cmd = add_int_val(cmd, ALARM_SERVICE_AFFECTING, data.service_affecting);
    	cmd = add_int_val(cmd, ALARM_SUPPRESSION, data.suppression);
	}

	return cmd;
}

bool fm_snmp_util_gen_trap(CFmDBSession &sess, int type, SFmAlarmDataT &data) {

	bool rc = true;
	fm_buff_t cmdbuff;
	fm_db_result_t res;
	std::string cmd, eid;

	if (!get_trap_dest_list(sess,res)) return false;

    if (&data != NULL) {
    	eid.assign(data.entity_instance_id);
        std::string region_name = fm_db_util_get_region_name(sess);
    	std::string sys_name = fm_db_util_get_system_name(sess);
    	if (sys_name.length() != 0){
    		eid = sys_name + "."+ eid;
    	}
        if (region_name.length() != 0){
                eid = region_name + "."+ eid;
        }
    	strncpy(data.entity_instance_id, eid.c_str(),
			sizeof(data.entity_instance_id)-1);
    }

	fm_db_result_t::iterator it = res.begin();
	fm_db_result_t::iterator end = res.end();

	for (; it != end; ++it){
		memset(&(cmdbuff[0]), 0, cmdbuff.size());
		cmd.clear();
		std::string ip = (*it)[FM_TRAPDEST_IP_COLUMN];
		std::string comm = (*it)[FM_TRAPDEST_COMM_COLUMN];
		cmd = format_trap_cmd(sess,type, data, ip, comm);

		//FM_INFO_LOG("run cmd: %s\n", cmd.c_str());
		char *pline = &(cmdbuff[0]);
		FILE *op = popen(cmd.c_str(),"r");
		if (op==NULL) {
			FM_ERROR_LOG("popen() failed, errno: (%d) (%s)\n",
					errno, strerror(errno));
			rc = false;
		}
		while (fgets(pline,cmdbuff.size(),op)!=NULL) {
			FM_ERROR_LOG("Trap error message: (%s)\n", pline);
		}
		fclose(op);
	}
	return rc;
}

static bool fm_snmp_get_db_connection(std::string &connection){
	CfmFile f;
	const char *fn = "/etc/fm.conf";
	std::string sql_key = FM_SQL_CONNECTION;
	std::string delimiter = "=";
	std::string line, key, value;
	size_t pos = 0;

	if (!f.open(fn, CfmFile::READ, false)){
		FM_ERROR_LOG("Failed to open config file: %s\n", fn);
		exit (-1);
	}

	while (true){
		if (!f.read_line(line)) break;

		if (line.size() == 0) continue;

		pos = line.find(delimiter);
		key = line.substr(0, pos);
		if (key == sql_key){
			value = line.erase(0, pos + delimiter.length());
			// Don't log sql_connection, as it has a password
			//FM_DEBUG_LOG("Found it: (%s)\n", value.c_str());
			connection = value;
			return true;
		}
	}

	return false;;
}


extern "C" {
bool fm_snmp_util_create_session(TFmAlarmSessionT *handle, const char* db_conn){

	std::string key = FM_SQL_CONNECTION;
	std::string conn;
	CFmDBSession *sess = new CFmDBSession;
	if (sess==NULL) return false;;

	if (db_conn == NULL){
		if (fm_snmp_get_db_connection(conn) != true){
			FM_ERROR_LOG("Fail to get db connection uri\n");
			delete sess;
			return false;
		}
		db_conn = conn.c_str();
	}

	if (sess->connect(db_conn) != true){
		FM_ERROR_LOG("Fail to connect to (%s)\n", db_conn);
		delete sess;
		return false;
	}
	*handle = sess;
	return true;
}

void fm_snmp_util_destroy_session(TFmAlarmSessionT handle) {
	CFmDBSession *sess = (CFmDBSession *)handle;

	if (sess != NULL){
		delete sess;
	}
}

bool fm_snmp_util_get_all_alarms(TFmAlarmSessionT handle,
		SFmAlarmQueryT *query) {

	assert(handle!=NULL);

	CFmDbAlarmOperation op;
	fm_db_result_t res;

	CFmDBSession &sess = *((CFmDBSession*)handle);

	if (!op.get_all_alarms(sess, &(query->alarm), &(query->num))) return false;

	return true;
}

bool fm_snmp_util_get_all_event_logs(TFmAlarmSessionT handle,
		SFmAlarmQueryT *query) {

	assert(handle!=NULL);

	CFmDbEventLogOperation op;
	fm_db_result_t res;

	CFmDBSession &sess = *((CFmDBSession*)handle);

	if (!op.get_all_event_logs(sess, &(query->alarm), &(query->num))) return false;

	return true;
}

}

