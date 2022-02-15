#!/usr/bin/env python3
import os
import sys

SecSizes=[128,256,512,1024,4096,0]
FDIDiskLabel = ""
class FDIDisk():
    def __init__(self,Sides,Tracks,Sectors,SecSize,Data,DataSize,Header):
        self.Sides = Sides
        self.Tracks = Tracks
        self.Sectors = Sectors
        self.SecSize = SecSize
        self.Data = Data
        self.DataSize = DataSize
        self.Header = Header

def newFDI(Sides,Tracks,Sectors,SecSize):
    # Find sector size code
    L = 0
    while SecSizes[L] and SecSizes[L]!=SecSize:
        if SecSizes[L]==0: return(0)
        L = L + 1
    # Allocate memory */
    K = int(Sides*Tracks*Sectors*SecSize+len(FDIDiskLabel))
    I = int(Sides*Tracks*(Sectors+1)*7+14)
    diskArray = bytearray(I+K)

    # FDI magic number
    diskArray[0] = ord('F')
    diskArray[1] = ord('D')
    diskArray[2] = ord('I')

    # Disk description
    diskArray[I:I+len(FDIDiskLabel)] = FDIDiskLabel.encode()
    #Write protection (1=ON)
    diskArray[3]  = 1
    diskArray[4]  = Tracks&0xFF
    diskArray[5]  = Tracks>>8
    diskArray[6]  = Sides&0xFF
    diskArray[7]  = Sides>>8
    # Disk description offset
    diskArray[8]  = I&0xFF
    diskArray[9]  = I>>8
    I += len(FDIDiskLabel)
    # Sector data offset
    diskArray[10] = I&0xFF
    diskArray[11] = I>>8
    # Track directory offset
    diskArray[12] = 0
    diskArray[13] = 0

    offset = 14
    J = 0
    K = 0
    while J<Sides*Tracks :
        # Create track entry
        diskArray[offset+0] = K&0xFF
        diskArray[offset+1] = (K>>8)&0xFF
        diskArray[offset+2] = (K>>16)&0xFF
        diskArray[offset+3] = (K>>24)&0xFF
        # Reserved bytes
        diskArray[offset+4] = 0
        diskArray[offset+5] = 0
        diskArray[offset+6] = Sectors
        # For all sectors on a track...
        I = 0
        N = 0
        offset+=7
        while I<Sectors:
            # Create sector entry
            diskArray[offset+0] = int(J/Sides)
            diskArray[offset+1] = J%Sides
            diskArray[offset+2] = I+1
            diskArray[offset+3] = L
            # CRC marks and "deleted" bit (D00CCCCC)
            diskArray[offset+4] = (1<<L)
            diskArray[offset+5] = N&0xFF
            diskArray[offset+6] = N>>8

            I+=1
            offset+=7
            N+=SecSize

        K+=Sectors*SecSize
        J+=1

    return diskArray

def analyzeMsxDsk(dskFile,fileName):
    dskFile.seek(0, os.SEEK_END)
    size = dskFile.tell()
    dskFile.seek(0, os.SEEK_SET)

    # Read header
    content = dskFile.read(32)
    dskFile.seek(0, os.SEEK_SET)
    if content[0]!=0xE9 and content[0]!=0xEB :
        return bytearray(0)

    # Check media descriptor
    if(content[21]<0xF8) :
        return bytearray(0)

    # Compute disk geometry
    K = int(content[26]+(content[27]<<8))      # Heads
    N = int(content[24]+(content[25]<<8))      # Sectors
    L = int(content[11]+(content[12]<<8))      # SecSize
    I = int(content[19]+(content[20]<<8))      # Total S.
    if K == 0 or N == 0:
        I = int(0)                             # Tracks
    else :
        I = int(I/K/N)                         # Tracks

    # Number of heads CAN BE WRONG */
    if I == 0 or N == 0 or L == 0:
        K = int(0)
    else :
        K = int(size/I/N/L)

    # Create a new disk image
    diskArray = newFDI(K,I,N,L)

    # Make sure we do not read too much data
    I = K*I*N*L
    if size > I : size = I

    offset = diskArray[10]+(diskArray[11]<<8)
    diskArray[offset:len(diskArray)] = dskFile.read(len(diskArray) - offset)

    return diskArray

def analyzeRawDsk(dskFile,fileName):
    dskFile.seek(0, os.SEEK_END)
    size = dskFile.tell()
    dskFile.seek(0, os.SEEK_SET)

    if size == 2*80*9*512:
        diskArray = newFDI(2,80,9,512)
        offset = diskArray[10]+(diskArray[11]<<8)
        diskArray[offset:len(diskArray)] = dskFile.read(len(diskArray) - offset)
    else :
        diskArray = bytearray(0)

    return diskArray

n = len(sys.argv)

if n < 2: print("Usage :\ndsk2fdi.py file.dsk\n"); sys.exit(0)

# Open file and find its size
print("Opening "+sys.argv[1])
dskFile = open(sys.argv[1], 'rb')
fdiArray = analyzeMsxDsk(dskFile,sys.argv[1])
if len(fdiArray) > 0 :
    outFile = sys.argv[1].replace(".dsk",".dsk.fdi")
    print("File is MSX dsk file, saving "+outFile)
    newfile=open(outFile,'wb')
    newfile.write(fdiArray)
    newfile.close()
    dskFile.close()
    print("Done")
    sys.exit(0)
else :
    fdiArray = analyzeRawDsk(dskFile,sys.argv[1])
    if len(fdiArray) > 0 :
        outFile = sys.argv[1].replace(".dsk",".dsk.fdi")
        print("File is RAW dsk file, saving "+outFile)
        newfile=open(outFile,'wb')
        newfile.write(fdiArray)
        newfile.close()
        dskFile.close()
        print("Done")
        sys.exit(0)

print("Failed")
#TODO : check if file is not already a FDI file

dskFile.close()
sys.exit(-1)