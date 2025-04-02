from datetime import datetime

from influxdb_client import InfluxDBClient, Point, WritePrecision
from influxdb_client.client.write_api import SYNCHRONOUS
import configparser
import pandas as pd
import time
import warnings
from influxdb_client.client.warnings import MissingPivotFunction
import os
warnings.simplefilter("ignore", MissingPivotFunction)

def dd_gmasim_server(ip_address, port, token, org, bucket):
    while True:
        time.sleep (0.01)
        url="http://" + ip_address + ":"+port
        with InfluxDBClient(url=url, token=token, org=org) as client:
            query_api = client.query_api()
            df = query_api.query_data_frame('from(bucket: "'+bucket+'")' 
                +'|> range(start: -10s)|> last()'
                +'|> filter(fn: (r) => r._measurement == "AI")'
                #+'|> filter(fn: (r) => r._field == "start_ts" or r._field == "end_ts" or r._field == "tx_rate" or r._field == "owd" or r._field == "max_rate")'
                )
            if df.empty:
                continue
                
            # we detects something in the database
            #print("run DD-GMASim")

def main():
    print("DD-GMASim Server is Up!")

    config = configparser.ConfigParser()
    config.read('gma.ini')

    dd_gmasim_server(config['InfluxDB']['ip_address'],
        config['InfluxDB']['port'], 
        config['InfluxDB']['token'], 
        config['InfluxDB']['org'], 
        config['InfluxDB']['bucket'])

    #os.system('./ns3 configure --build-profile=optimized')
    #os.system('./ns3 build')
    #os.system('./ns3 run scratch/gma-ml-playground.cc --cwd=output')
if __name__ == "__main__":
    main()