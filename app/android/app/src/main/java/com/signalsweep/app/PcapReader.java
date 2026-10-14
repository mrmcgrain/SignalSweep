package com.signalsweep.app;

import java.io.*;
import java.nio.*;
import java.util.Arrays;

/** Bounded classic-PCAP framing and radiotap metadata. Retains complete received frames. */
final class PcapReader {
    static final class Packet {
        byte[] record,frame;
        int frequency=0,channel=0,rssi=-127;
        boolean badFcs=false,malformed=false;
    }
    final InputStream in;
    final byte[] header;
    final ByteOrder order;
    final int linkType;
    PcapReader(InputStream stream) throws IOException {
        in=stream;header=read(24);
        int magic=ByteBuffer.wrap(header).order(ByteOrder.LITTLE_ENDIAN).getInt();
        if(magic==0xa1b2c3d4||magic==0xa1b23c4d)order=ByteOrder.LITTLE_ENDIAN;
        else if(magic==0xd4c3b2a1||magic==0x4d3cb2a1)order=ByteOrder.BIG_ENDIAN;
        else throw new IOException("Unsupported PCAP magic");
        ByteBuffer b=ByteBuffer.wrap(header).order(order);
        if(b.getShort(4)!=2||b.getShort(6)!=4)throw new IOException("Unsupported PCAP version");
        linkType=b.getInt(20)&0xffff;
        if(linkType!=127&&linkType!=105)throw new IOException("Expected radiotap or 802.11, got link type "+linkType);
    }
    private byte[] read(int n)throws IOException{byte[] b=new byte[n];int i=0;while(i<n){int k=in.read(b,i,n-i);if(k<0)throw new EOFException();i+=k;}return b;}
    Packet next()throws IOException {
        byte[] h=read(16);ByteBuffer b=ByteBuffer.wrap(h).order(order);int n=b.getInt(8),original=b.getInt(12);
        if(n<0||n>65535||original<n)throw new IOException("Invalid/truncated PCAP record size");
        byte[] raw=read(n);Packet p=new Packet();p.record=new byte[16+n];System.arraycopy(h,0,p.record,0,16);System.arraycopy(raw,0,p.record,16,n);
        try { decode(p,raw,n); } catch(IOException e){p.malformed=true;p.frame=new byte[0];}
        return p;
    }
    private void decode(Packet p,byte[] raw,int n)throws IOException {
        int start=0,flags=0;
        if(linkType==127){
            if(n<8||raw[0]!=0)throw new IOException("Malformed radiotap header");
            ByteBuffer r=ByteBuffer.wrap(raw).order(ByteOrder.LITTLE_ENDIAN);start=r.getShort(2)&65535;
            if(start<8||start>n)throw new IOException("Invalid radiotap length");
            int present=r.getInt(4),pos=8,ext=present;
            while((ext&0x80000000)!=0){if(pos+4>start)throw new IOException("Truncated radiotap bitmap");ext=r.getInt(pos);pos+=4;}
            int[] align={8,1,1,2,2,1,1,2},size={8,1,1,4,2,1,1,2};
            for(int i=0;i<8;i++)if((present&(1<<i))!=0){pos=(pos+align[i]-1)&~(align[i]-1);if(pos+size[i]>start)throw new IOException("Truncated radiotap field");if(i==1)flags=raw[pos]&255;if(i==3)p.frequency=r.getShort(pos)&65535;if(i==5)p.rssi=raw[pos];pos+=size[i];}
            p.badFcs=(flags&0x40)!=0;
        }
        int end=n-((flags&0x10)!=0?4:0);if(end<start)throw new IOException("Missing frame/FCS");
        p.frame=Arrays.copyOfRange(raw,start,end);
        int f=p.frequency;p.channel=f==2484?14:f>=2412&&f<=2472?(f-2407)/5:f>=5000&&f<5925?(f-5000)/5:f>=5955?(f-5950)/5:0;
    }
}
