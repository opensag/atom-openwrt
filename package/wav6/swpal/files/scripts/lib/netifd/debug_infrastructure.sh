number_of_debug_configs=10

debug_infrastructure_config_add_string() {
	local config_name_prefix="$1"
	for i in $(seq 1 $number_of_debug_configs); do
		config_add_string $config_name_prefix$i
	done
}

debug_infrastructure_json_get_vars() {
	local config_name_prefix="$1"
	for i in $(seq 1 $number_of_debug_configs); do
		json_get_vars $config_name_prefix$i
	done
}

debug_infrastructure_append() {
	local config_name_prefix="$1"
	local config_type="$2"

	for i in $(seq 1 $number_of_debug_configs); do
		eval debug_config=( \"\${$config_name_prefix$i}\" )
		debug_config_value=$debug_config

		if [ -n "$debug_config_value" ]; then
			append $config_type "$debug_config_value" "$N"
		fi
	done
}

debug_infrastructure_execute_iw_command(){
	local config_name_prefix="$1"
	local radio_idx="$2"

	for i in $(seq 1 $number_of_debug_configs); do
		eval debug_config=( \"\${$config_name_prefix$i}\" )
		debug_config_value=$debug_config

		if [ -n "$debug_config_value" ]; then
			eval "iw wlan$radio_idx iwlwav $debug_config_value"
		fi
	done
}