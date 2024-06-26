"""
USAGE: spark-submit --driver-memory <size> kmeans.py <path>
"""

from pyspark.sql import SparkSession
from pyspark import SparkConf, SparkContext
from pyspark.ml.clustering import KMeans
from pyspark.ml.feature import VectorAssembler
import struct
import pandas as pd
import sys
from jarvis_util import *

# Get cmd
path = sys.argv[1]
k = int(sys.argv[2])
max_iter = int(sys.argv[3])

# Initialize Spark
spark = SparkSession.builder.appName("LargeFileProcessing").getOrCreate()
def make_parquet_rdd():
    parquet_path = path
    rdd = spark.read.parquet(parquet_path)
    feature_cols = ["x", "y"]
    assembler = VectorAssembler(inputCols=feature_cols, outputCol="features")
    hermes_rdd = assembler.transform(rdd)
    return hermes_rdd

# Read training data and fit
ColorPrinter.print("Beginning KMeans", Color.GREEN)
rdd = make_parquet_rdd()
kmeans = KMeans(k=k, maxIter=max_iter, seed=1)
model = kmeans.fit(rdd)
ColorPrinter.print("KMeans Complete", Color.GREEN)
print(model.clusterCenters())

# Stop Spark
spark.stop()
