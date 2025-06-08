#!/usr/bin/python3
import numpy as np
import scipy.stats as st
import subprocess
import glob
import time
import sys
import math
import re
import argparse
import matplotlib
matplotlib.use('PDF')
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages


def dist(x,y):
    s = 0.0
    for k in x:
        if k not in y:
            s = s + x[k]
        else:
            s = s + math.fabs(x[k] - y[k])
            
    for k in y:
        if k not in x:
            s = s + y[k]
            
            
    return s

def heatmap2d(arr, name):
    pp = PdfPages(name + '.pdf')
    plt.imshow(arr, cmap='viridis')
    plt.colorbar()
    plt.savefig(pp,format='pdf',dpi=300)
    pp.close()

if __name__ == '__main__':
    vectors = []
    keys = set()
    with open(sys.argv[1], 'r') as in_:
        for line in in_:
            if line[0] != 'T':
                continue
            line = line[1:len(line)]
            toks = line.split(' ')
            d = dict()
            s = 0.0
            for t in toks:
                m = re.search(r':(\d+):(\d+)', t)
                if m == None:
                    continue
                g = m.groups()
                i = int(g[0])
                if i not in keys:
                    keys.add(i)
                s = s + float(g[1])
                d[i] = float(g[1])
            #normalize
            for k in d:
                d[k] = d[k] / s
                
            vectors.append(d)

    mtx = np.zeros((len(vectors), len(vectors)))
    mtx.fill(2.0)
    for i in range(0, len(vectors)):
        for j in range(i, len(vectors)):
            d = dist(vectors[i], vectors[j])
            mtx[i][j] = d
            mtx[j][i] = d
            
    heatmap2d(mtx, sys.argv[1])
