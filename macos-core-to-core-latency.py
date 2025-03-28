import argparse
import math
from matplotlib import pyplot as plt
import numpy as np
import os
import pandas as pd
import subprocess

def cpuNameAndCoreCount():
    cpuName, coreCount = None, None

    result = subprocess.run(['system_profiler', 'SPHardwareDataType'], capture_output=True, text=True)
    output = result.stdout
    
    for line in output.splitlines():
        if 'Chip:' in line:
            cpuName = line.replace('Chip:', '').strip()
        elif 'Total Number of Cores:' in line:
            coreCount_ = line.replace('Total Number of Cores:', '')
            coreCount  = coreCount_[:coreCount_.find('(')].strip()

    return cpuName, coreCount

if __name__ == '__main__':

    parser = argparse.ArgumentParser()
    parser.add_argument('logFile')
    parser.add_argument('-v', '--verbose'  , action='store_true')
    parser.add_argument('-n', '--cpu-name' , help='cpu name override')
    parser.add_argument('-c', '--cpu-cores', help='cpu core count override')
    args = parser.parse_args()

    plotFilename = os.path.splitext(args.logFile)[0]+'.png'

    cpuName, cpuCores = cpuNameAndCoreCount()
    if args.cpu_name:
        cpuName = args.cpu_name
        cpuCores = args.cpu_cores
    if cpuName:
        if cpuCores: cpuString = f'{cpuName} ({cpuCores}C)'
        else        : cpuString = f'{cpuName}'
        print('CPU:', cpuString)
    else:
        cpuString = None
        print('CPU: N/A')

    df0 = pd.read_csv(
        args.logFile,
        names = ['fromIndex', 'toIndex', 'fromCore', 'toCore', 'fromCoreLatencyNs', 'toCoreLatencyNs', 'fromCoreFrequencyBeforeGhz', 'fromCoreFrequencyAfterGhz', 'toCoreFrequencyBeforeGhz', 'toCoreFrequencyAfterGhz'],
        converters={
            'fromCore'                   : lambda x: "{:5}".format(int(x)),
            'toCore'                     : lambda x: "{:5}".format(int(x)),
            'fromCoreLatencyNs'          : lambda x: float(x.strip(' ns')),
            'fromCoreFrequencyBeforeGhz' : lambda x: float(x.strip(' GHz')),
        },
        comment='#')
    
    if args.verbose:
        with pd.option_context('display.max_columns', None, 'display.width', None):
            print("Partial csv dump:")
            print(df0)
            print()

    df0.drop(['fromIndex', 'toIndex'], axis = 1, inplace=True);

    coreIds = [ str(x) for x in sorted([ int(x.strip()) for x in df0['fromCore'].unique() ])]
    coreCount = len(df0['fromCore'].unique())
    coreIndices = list(range(0, coreCount))

    print("Num cores:", coreCount)

    df1 = df0.groupby(['fromCore', 'toCore']).agg({
        'toCoreLatencyNs' : [
            'count',
            'min',
            ('5%', lambda x:x.quantile(0.05)),
            ('25%', lambda x:x.quantile(0.25)),
            'mean',
            'median',
            ('75%', lambda x:x.quantile(0.75)),
            ('95%', lambda x:x.quantile(0.95)),
            'max']
    }).round(2)

    print("Samples per core pair: min:", df1['toCoreLatencyNs']['count'].min(), "avg:", df1['toCoreLatencyNs']['count'].mean(), "max:", df1['toCoreLatencyNs']['count'].max())

    if args.verbose:
        with pd.option_context('display.max_rows', None, 'display.max_columns', None, 'display.width', None):
            print("Detailed Core-to-core latency aggregates (ns):")
            print(df1)
            print()

    df2 = df1.reset_index()


    df3 = df2.pivot(index='fromCore', columns='toCore')['toCoreLatencyNs']['median']

    minCell = df3.min().min()
    maxCell = df3.max().max()

    with pd.option_context('display.max_rows', None, 'display.max_columns', None, 'display.width', None, 'display.precision', 0):
        print("Median Core-to-core latency (ns):")
        print(df3)

    fig, ax = plt.subplots(figsize=(8, 8), dpi=200)
    hm = ax.matshow(df3)
    hm.get_cmap().set_bad(color='gray')

    cpuStringForTitle = cpuString + ' ' if cpuString else ''
    fontsize = 9 if maxCell >= 100 else 10
    isnan = np.isnan(df3)
    blackAt = (minCell+3*maxCell)/4

    ii = 0
    for index, row in df3.iterrows():
        jj = 0
        for column in df3.columns:
            v = row[column]
            t = "" if math.isnan(v) else f"{v:.0f}"
            c = "w" if v < blackAt else "k"
            plt.text(jj, ii, t, ha="center", va="center", color=c, fontsize=fontsize)

            jj += 1
        ii += 1

    ax.set_xticks(ticks=coreIndices, labels=coreIds, rotation=45)
    ax.set_yticks(ticks=coreIndices, labels=coreIds)
    ax.set_title(f'{cpuStringForTitle}Core-to-Core Latency (ns)')
    ax.tick_params(bottom=False)

    cb = ax.figure.colorbar(hm, ax=ax, fraction=0.046, pad=0.04)
    cbTicks = [minCell] + list(filter(lambda x: x > minCell and x < maxCell, list(cb.get_ticks()))) + [maxCell]
    cb.set_ticks(cbTicks)
    cb.set_ticklabels([ f'{x:.0f}' for x in cbTicks])

    fig.tight_layout()
    fig.savefig(plotFilename, transparent=False, facecolor='white')

    print(f'Stored plot to {plotFilename}')