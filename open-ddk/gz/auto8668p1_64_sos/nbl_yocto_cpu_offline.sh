#!/bin/bash

cpus=("cpu2" "cpu3" "cpu7")


# 0：cpu3+5--yocto cpu3(pcpu0 pcpu1 pcpu4)+android cpu5(pcpu2 cpu3 pcpu5 pcpu6 pcpu7)
# 1：cpu3+3+2--yocto cpu3(pcpu0 pcpu1 pcpu4)+android cpu3(pcpu2 pcpu3 pcpu7)+share two cpu(pcpu5 pcpu6)
# 2：cpu3+4+1--yocto cpu3(pcpu0 pcpu1 pcpu4)+android cpu4(pcpu2 pcpu3 pcpu6 pcpu7)+share one cpu(pcpu5)

value=2

#cpus to close
if [ $value -eq 0 ]; then
    echo "cpu 3+5"
	cpus=("cpu2" "cpu3" "cpu5" "cpu6" "cpu7")
elif [ $value -eq 1 ]; then
    echo "cpu 3+3+2"
	cpus=("cpu2" "cpu3" "cpu7")
elif [ $value -eq 2 ]; then
    echo "cpu 3+4+1"
    cpus=("cpu2" "cpu3" "cpu6" "cpu7")
fi

sleep 30

while true; do
#    value=$(getprop sys.boot_completed.android)
#    if [ "$value" = "1" ]; then
#        echo "Android boot completed!"
		for cpu in "${cpus[@]}"; do
			online_path="/sys/devices/system/cpu/${cpu}/online"

			if [ -f "$online_path" ]; then
				while true; do
					status=$(cat "$online_path")

					if [ "$status" -eq 0 ]; then
						echo "$cpu is already offline."
						break
					fi

					echo "Attempting to set $cpu offline..."
					echo 0 > "$online_path"

					sleep 1

					new_status=$(cat "$online_path")
					if [ "$new_status" -eq 0 ]; then
						echo "$cpu successfully set to offline."
						break
					else
						echo "Failed to set $cpu offline. Retrying..."
					fi
				done
			else
				echo "$online_path does not exist. Skipping $cpu."
			fi
		done
        break
#    fi
#    echo "Android is not bootup, sleeping for 10 second"
#    sleep 10
done

# Restore minimum adjustment frequency
echo 0  > /sys/devices/system/cpu/cpufreq/policy0/scaling_min_freq
echo 0  > /sys/devices/system/cpu/cpufreq/policy4/scaling_min_freq

# set cpu 5 as share core(cpu 4 is share with TBox. Treat it as exclusive core)
echo "Setting share_core mask for sched(0x20)"
share_core_path="/sys/module/scheduler/parameters/share_core"
if [ -f "$share_core_path" ]; then
	echo 20 > "$share_core_path"
else
	echo "$share_core_path does not exist. Skip setting."
fi
chmod 444 "$share_core_path"
