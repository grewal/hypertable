#!/usr/bin/env bash

HT_HOME=${INSTALL_DIR:-"$HOME/hypertable/current"}
HYPERTABLE_HOME=$HT_HOME
PIDFILE=$HT_HOME/run/Hypertable.RangeServer.pid
LAUNCHER_PIDFILE=$HT_HOME/run/Hypertable.RangeServerLauncher.pid
DUMP_METALOG=$HT_HOME/bin/dump_metalog
MY_IP=`$HT_HOME/bin/ht system_info --my-ip`
METALOG="/hypertable/servers/rs1/log/range_txn/0"
RANGE_SIZE=${RANGE_SIZE:-"7M"}

# Kill launcher if running & store pid of this launcher
if [ -f $LAUNCHER_PIDFILE ]; then
  kill -9 `cat $LAUNCHER_PIDFILE`
  \rm -f $LAUNCHER_PIDFILE
fi
echo "$$" > $LAUNCHER_PIDFILE

# Kill RangeServer if running
if [ -f $PIDFILE ]; then
  kill -9 `cat $PIDFILE`
  \rm -f $PIDFILE
fi

$HT_HOME/bin/ht Hypertable.RangeServer --verbose --pidfile=$PIDFILE \
    --Hypertable.RangeServer.CellStore.DefaultBlockSize=1K \
    --Hypertable.RangeServer.MaintenanceThreads=8 \
    --Hypertable.RangeServer.Maintenance.Interval=100 $@

# Exit if base run
if [ -z $1 ]; then
    \rm -f $LAUNCHER_PIDFILE
    exit
fi

echo ""
echo "!!!! CRASH ($@) !!!!"
echo ""

$HT_HOME/bin/ht Hypertable.RangeServer --pidfile=$PIDFILE --verbose \
    --Hypertable.RangeServer.CellStore.DefaultBlockSize=1K \
    --Hypertable.RangeServer.Maintenance.Interval=100

\rm -f $LAUNCHER_PIDFILE
