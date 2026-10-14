package com.signalsweep.app;

import android.content.Context;
import java.io.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicBoolean;

/** Root supervisor owns Wi-Fi only while its app heartbeat pipe stays alive. */
final class NetHunterCapture {
    interface Listener { void ready();void packet(PcapReader.Packet p);void ended(String message); }
    private Process process;
    private final AtomicBoolean active=new AtomicBoolean(false);
    private ScheduledExecutorService heartbeat;
    private Thread reader;
    private byte[] header;
    private FileOutputStream recording,bleRecording;
    private File captureFile,bleFile;
    private long packets=0,blePackets=0,bytes=0;
    private String diagnostics="";
    synchronized void start(Context context,Listener listener)throws IOException {
        if(active.get())throw new IOException("NetHunter scanner is already running");
        File script=new File(context.getFilesDir(),"nethunter-capture.sh");
        try(InputStream in=context.getAssets().open("nethunter-capture.sh");FileOutputStream out=new FileOutputStream(script)){byte[] b=new byte[4096];int n;while((n=in.read(b))!=-1)out.write(b,0,n);}
        active.set(true);header=null;diagnostics="";
        process=new ProcessBuilder("su","-c","/system/bin/sh "+script.getAbsolutePath()).start();
        Thread errors=new Thread(()->{try(BufferedReader in=new BufferedReader(new InputStreamReader(process.getErrorStream()))){String line;while((line=in.readLine())!=null){synchronized(this){diagnostics=(diagnostics+"\n"+line);if(diagnostics.length()>3000)diagnostics=diagnostics.substring(diagnostics.length()-3000);}}}catch(IOException ignored){}},"ssweep-root-status");errors.start();
        heartbeat=Executors.newSingleThreadScheduledExecutor();
        heartbeat.scheduleAtFixedRate(()->{try{process.getOutputStream().write("alive\n".getBytes());process.getOutputStream().flush();}catch(IOException ignored){}},0,1,TimeUnit.SECONDS);
        reader=new Thread(()->{
            try {PcapReader in=new PcapReader(process.getInputStream());synchronized(this){header=in.header;}
                if(active.get())listener.ready();
                while(true){PcapReader.Packet p=in.next();if(!active.get())continue;synchronized(this){if(recording!=null){recording.write(p.record);packets++;bytes+=p.record.length;}}listener.packet(p);}
            } catch(Exception e){if(active.get())listener.ended("NetHunter capture stopped: "+e.getMessage()+" "+diagnostics);}
            finally {stop();}
        },"ssweep-pcap");reader.start();
    }
    synchronized void beginRecording(Context context)throws IOException {
        if(!active.get()||header==null)throw new IOException("NetHunter raw stream is not ready");
        if(recording!=null)throw new IOException("Capture already recording");
        File dir=context.getExternalFilesDir("captures");if(dir==null)throw new IOException("Capture storage unavailable");
        String name="signalsweep-"+System.currentTimeMillis();captureFile=new File(dir,name+".pcap");bleFile=new File(dir,name+"-ble.jsonl");
        FileOutputStream p=new FileOutputStream(captureFile),b=null;
        try {p.write(header);b=new FileOutputStream(bleFile);recording=p;bleRecording=b;packets=blePackets=0;bytes=header.length;}catch(IOException e){p.close();if(b!=null)b.close();throw e;}
    }
    synchronized void ble(String json)throws IOException{if(bleRecording!=null){bleRecording.write((json+"\n").getBytes(java.nio.charset.StandardCharsets.UTF_8));blePackets++;}}
    synchronized String endRecording()throws IOException {
        if(recording!=null){recording.getFD().sync();recording.close();recording=null;}
        if(bleRecording!=null){bleRecording.getFD().sync();bleRecording.close();bleRecording=null;}
        return captureFile==null?"":captureFile.getAbsolutePath();
    }
    synchronized long[] counts(){return new long[]{packets,blePackets,bytes};}
    synchronized boolean isRecording(){return recording!=null;}
    synchronized String recordingPath(){return recording!=null&&captureFile!=null?captureFile.getAbsolutePath():"";}
    synchronized String blePath(){return bleFile==null?"":bleFile.getAbsolutePath();}
    void stop(){
        if(!active.getAndSet(false))return;
        if(heartbeat!=null)heartbeat.shutdownNow();
        try{process.getOutputStream().close();}catch(Exception ignored){}
        try{endRecording();}catch(IOException ignored){}
        // The root shell restores mode/service before exiting. Don't kill it first.
        try{if(!process.waitFor(8,TimeUnit.SECONDS))process.destroy();}catch(InterruptedException e){Thread.currentThread().interrupt();}
    }
}
