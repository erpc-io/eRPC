#!/usr/bin/env bash
killall ib_write_bw
ib_write_bw --gid-index=0 --port=3185 --ib-dev=mlx5_0 --ib-port=1 --size=8388608
