import struct
import sys
import socket
import binascii

DATA_CHUNK_SIZE = 1024

# Sample usage: send.py <wav file> <target IP> <port>

def read_uint16(file):
  return struct.unpack("<H", file.read(2))[0]

def read_uint32(file):
  return struct.unpack("<I", file.read(4))[0]

with open(sys.argv[1], "rb") as f:
  while True:
    chunk_id = f.read(4)
    chunk_size = read_uint32(f)
    
    if chunk_id == "RIFF":
      # header chunk
      
      format = str(f.read(4))
      
      if format != "WAVE":
        print "Invalid format: {0}".format(format)
        sys.exit()
    elif chunk_id == "fmt ":
      # format chunk
      
      if chunk_size != 16:
        print "WARNING: fmt chunk size != 16: {0}".format(chunk_size)
      
      audio_format = read_uint16(f)
      
      if audio_format != 1:
        print "WARNING: audio_format != 1(PCM)?"
      
      num_channels = read_uint16(f)
      
      print "File is {0}".format("stereo" if num_channels == 2 else "mono")
      
      sample_rate = read_uint32(f)
      
      print "Sample rate: {0}".format(sample_rate)
      
      byte_rate = read_uint32(f)
      block_align = read_uint16(f)
      bits_per_sample = read_uint16(f)
      
      print "Bits per sample: {0}".format(bits_per_sample)
    elif chunk_id == "data":
      # data chunk
      
      print "Data size: {0}".format(chunk_size)
                
      s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
      
      remaining_data = chunk_size
      
      # for debugging
      remaining_data = 2000
      sequence_number = 1
      while remaining_data > 0:
        size_to_read = DATA_CHUNK_SIZE
        
        if (remaining_data < size_to_read): size_to_read = remaining_data
        
        data = bytearray()
        
        data.extend(struct.pack("<I", sequence_number))
        
        chunk = f.read(size_to_read)
        
        data.extend(chunk)
        
        print binascii.hexlify(data)
        
        print "Sending packet {0}".format(sequence_number)
        
        s.sendto(data, (sys.argv[2], int(sys.argv[3])))
        
        sequence_number += 1
        remaining_data -= size_to_read
      
      break
    else:
      # unknown chunk
      
      print "Unknown chunk type: {0}".format(chunk_id)
      print "Skipping {0} bytes".format(chunk_size)
      
      f.read(chunk_size)

print "Done"