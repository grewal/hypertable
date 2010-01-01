#!/usr/bin/env bash
#
# Copyright (C) 2009  Luke Lu (llu@hypertable.org)
#
# This file is part of Hypertable.
#
# Hypertable is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or any later version.
#
# Hypertable is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Hypertable. If not, see <http://www.gnu.org/licenses/>
#

ulimit -c unlimited

die() {
  echo "$@"
  exit 1
}

server_pidfile() {
  case $1 in
    hyperspace)         echo $HYPERTABLE_HOME/run/Hyperspace.pid;;
    dfsbroker)          echo $HYPERTABLE_HOME/run/DfsBroker.*.pid;;
    master)             echo $HYPERTABLE_HOME/run/Hypertable.Master.pid;;
    rangeserver)        echo $HYPERTABLE_HOME/run/Hypertable.RangeServer.pid;;
    thriftbroker)       echo $HYPERTABLE_HOME/run/ThriftBroker.pid;;
    *) echo "unknown";  echo "ERROR: unknown service: $1" >&2; return 1
  esac
}

check_pidfile() {
  for f in $@; do
    pid=`cat $f 2> /dev/null`
    service=`basename $f .pid`
    if [ "$pid" ]; then
      ret=`ps -fp $pid | grep $pid`
      [ $? != 0 ] && return 1
      echo "$service appears to be running ($pid):"
    else
      name=`basename $f`
      ret=`ps -ef | grep -v grep | grep $name`
      [ $? != 0 ] && return 1
      echo "$service seems to be running:"
    fi
    echo $ret
  done
}

stop_server() {
  pidre='.pid$'
  for server in $@; do
    if [[ $server =~ $pidre ]]; then
      pidfile=$server
    else
      pidfile=`server_pidfile $server`
    fi
    if [ -f "$pidfile" ] ; then
      pid=`cat $pidfile`
      echo "Killing `basename $pidfile` $pid"
      kill -9 $pid
      rm $pidfile
    fi
  done
}

should_wait() {
  ret=$1
  become=$2
  case $become in
    come*up)    return $((!$ret));;
    shutdown)   return $(($ret));;
    *)          echo "ERROR: unexpected expectation: $become";;
  esac
  exit 1
}

show_success() {
  server_desc=$1
  become=$2
  case $become in
    come*up)    echo "Started $server_desc";;
    shutdown)   echo "Shutdown $server_desc complete";;
    *)          echo "ERROR: unexpected expectation: $become";;
  esac
}

check_server() {
  $HYPERTABLE_HOME/bin/serverup --silent "$@"
}

wait_for_server() {
  become=$1; shift
  server=$1; shift
  server_desc=$1; shift
  max_retries=${max_retries:-20}
  report_interval=${report_interval:-5}

  check_server $server
  ret=$?
  retries=0
  while should_wait $ret "$become" && [ $retries -lt $max_retries ]; do
    let retries=retries+1
    let report=retries%$report_interval
    [ $report == 0 ] && echo "Waiting for $server_desc to $become..."
    sleep 1
    check_server $server
    ret=$?
  done
  if should_wait $ret "$become"; then
    echo "ERROR: $server_desc did not $become"
    check_pidfile `server_pidfile $server`
    return 1
  else
    show_success "$server_desc" "$become"
  fi
}

wait_for_server_up() {
  wait_for_server "come up" "$@"
  check_startlog $? $startlog
}

wait_for_server_shutdown() {
  wait_for_server "shutdown" "$@"
}

set_start_vars() {
  pidfile=$HYPERTABLE_HOME/run/$1.pid
  logfile=$HYPERTABLE_HOME/log/$1.log
  startlog=/tmp/start-$1$$.log
  if type cronolog > /dev/null 2>&1; then
    logger="cronolog --link $logfile \
      $HYPERTABLE_HOME/log/archive/%Y-%m/%d/$1.log"
  else
    logger=
  fi
}

check_startlog() {
  [ -s $2 ] && cat $2
  [ $1 != 0 ] && return 1
  rm -f $2
}

exec_server() {
  servercmd=$1; shift
  if [ "$logger" ]; then
    ($HEAPCHECK $VALGRIND $HYPERTABLE_HOME/bin/$servercmd --pidfile $pidfile "$@" 2>&1 |
        $logger) &> $startlog &
  else
    $HEAPCHECK $VALGRIND $HYPERTABLE_HOME/bin/$servercmd --pidfile $pidfile "$@" \
        &> $logfile &
  fi
}

start_server() {
  server=$1; shift
  servercmd=$1; shift
  pidname=$1; shift
  set_start_vars $pidname
  check_pidfile $pidfile && return 0

  check_server $server
  if [ $? != 0 ] ; then
    exec_server $servercmd --verbose "$@"
    wait_for_server_up $server "$pidname"
  else
    echo "WARNING: $pidname already running."
  fi
}

# Sanity check
[ "$HYPERTABLE_HOME" ] || die "ERROR: HYPERTABLE_HOME is not set"
versionre='/([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+|current)$'
[[ $HYPERTABLE_HOME =~ $versionre ]] ||
    die "ERROR: Invalid HYPERTABLE_HOME: $HYPERTABLE_HOME"

# Make sure log and run directories exist
[ -d $HYPERTABLE_HOME/run ] || mkdir $HYPERTABLE_HOME/run
[ -d $HYPERTABLE_HOME/log ] || mkdir $HYPERTABLE_HOME/log

# Runtime libraries
export LD_LIBRARY_PATH="$HYPERTABLE_HOME/lib:$LD_LIBRARY_PATH"
export DYLD_LIBRARY_PATH="$LD_LIBRARY_PATH:$DYLD_LIBRARY_PATH"
