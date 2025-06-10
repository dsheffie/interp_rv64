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
#import numpy_ml
matplotlib.use('PDF')
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages


def heatmap2d(arr, name):
    pp = PdfPages(name + '.pdf')
    plt.imshow(arr, cmap='viridis')
    plt.colorbar()
    plt.savefig(pp,format='pdf',dpi=300)
    plt.title("Simpoint BBV Self-Similarity Matrix (Manhattan Distance)")
    plt.ylabel("BBV vector i")
    plt.xlabel("BBV vector j")    
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

    print('done loading, number vectors %d, num unique keys %d' % (len(vectors), len(keys)))
    dmtx = np.zeros((len(vectors), len(keys)+1))
    for i in range(0, len(vectors)):
        v = vectors[i]
        for k in v:
            dmtx[i][k] = float(v[k])
        
    print('done generating dense matrix')

    mtx = np.zeros((len(vectors), len(vectors)))
    sl = len(vectors) // 100
    for i in range(0, len(vectors)):
        for j in range(i, len(vectors)):
            d =  np.sum(np.abs(dmtx[i] - dmtx[j]))
            mtx[i][j] = d
            mtx[j][i] = d
            
    heatmap2d(mtx, sys.argv[1])
