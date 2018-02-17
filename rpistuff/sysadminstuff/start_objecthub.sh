#!/bin/bash
cd ~/theobject_server/;
source ./bin/activate;
authbind --deep python ./cherry.py
