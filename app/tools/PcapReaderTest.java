package com.signalsweep.app;
import java.io.*;
import java.nio.*;
import java.util.*;
public class PcapReaderTest {
    static void require(boolean b,String message){if(!b)throw new AssertionError(message);}
    static byte[] capture(ByteOrder order,byte[] raw){ByteBuffer b=ByteBuffer.allocate(40+raw.length).order(order);b.putInt(0xa1b2c3d4).putShort((short)2).putShort((short)4).putInt(0).putInt(0).putInt(65535).putInt(127).putInt(0).putInt(0).putInt(raw.length).putInt(raw.length).put(raw);return b.array();}
    public static void main(String[] args)throws Exception {
        byte[] raw=new byte[16+24+4];ByteBuffer b=ByteBuffer.wrap(raw).order(ByteOrder.LITTLE_ENDIAN);b.putShort(2,(short)16);b.putInt(4,(1<<1)|(1<<3)|(1<<5));raw[8]=0x10;b.putShort(10,(short)5180);raw[14]=-52;raw[16]=(byte)0x80;
        for(ByteOrder order:new ByteOrder[]{ByteOrder.LITTLE_ENDIAN,ByteOrder.BIG_ENDIAN}){
            PcapReader r=new PcapReader(new ByteArrayInputStream(capture(order,raw)));PcapReader.Packet p=r.next();require(p.frequency==5180&&p.channel==36&&p.rssi==-52&&p.frame.length==24&&!p.malformed,"radio metadata/FCS "+order);
            require(p.record.length==raw.length+16,"raw record preserved");
        }
        raw[0]=0x60;PcapReader.Packet bad=new PcapReader(new ByteArrayInputStream(capture(ByteOrder.LITTLE_ENDIAN,raw))).next();require(bad.malformed&&bad.frame.length==0,"driver non-radiotap packet skipped");
        for(int n=0;n<8;n++){PcapReader.Packet p=new PcapReader(new ByteArrayInputStream(capture(ByteOrder.LITTLE_ENDIAN,new byte[n]))).next();require(p.malformed,"short radiotap");}
        byte[] cap=capture(ByteOrder.LITTLE_ENDIAN,new byte[0]);ByteBuffer.wrap(cap).order(ByteOrder.LITTLE_ENDIAN).putInt(32,999999);try{new PcapReader(new ByteArrayInputStream(cap)).next();throw new AssertionError("oversize accepted");}catch(IOException expected){}
        if(args.length>0){int total=0,malformed=0;try(InputStream in=new FileInputStream(args[0])){PcapReader r=new PcapReader(in);while(true){try{PcapReader.Packet p=r.next();total++;if(p.malformed)malformed++;}catch(EOFException end){break;}}}System.out.println("Live PCAP framing: "+total+" records, "+malformed+" malformed driver records retained/skipped");}
        System.out.println("PASS radiotap endian, FCS, channel, short/oversized and raw preservation checks");
    }
}
