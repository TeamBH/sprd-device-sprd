package com.spreadtrum.android.eng;

import android.widget.TextView;
import android.app.Activity;
import android.os.Bundle;
import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import android.util.Log;
import android.content.Intent;

public class TextInfo extends Activity{
    static final String TAG = "TextInfo";

    private TextView mTextView;
    private String mText;

    private engfetch mEf;
    private String mATline;
    private int mSocketID;
    /*Add 20130201 Spreadst of 123080 ANR after press mannual assert  start */
    private int mStartN;
    /*Add 20130201 Spreadst of 123080 ANR after press mannual assert  end */
    private ByteArrayOutputStream outputBuffer;
    private DataOutputStream outputBufferStream;

    /** Called when the activity is first created. */
    @Override
    public void onCreate(Bundle savedInstanceState){
        super.onCreate(savedInstanceState);
        setContentView(R.layout.textinfo);

        mTextView = (TextView) findViewById(R.id.text_view);
        mEf = new engfetch();
        Intent intent = this.getIntent();
        mStartN = intent.getIntExtra("text_info", 0);

		switch (mStartN) {
		case 1:
			setTitle(R.string.sim_forbid_plmn);
			mSocketID = mEf.engopen();
			break;
		case 2:
			setTitle(R.string.sim_equal_plmn);
			mSocketID = mEf.engopen();
			break;
		default:
			Log.e(TAG, "mStartN:" + mStartN);
		}
        /*Add 20130201 Spreadst of 123080 ANR after press mannual assert  start */
        this.setDisplayText();
        /*Add 20130201 Spreadst of 123080 ANR after press mannual assert  end */
    }
    /*Add 20130201 Spreadst of 123080 ANR after press mannual assert  start */
    private void  setDisplayText(){
        new Thread(
                new Runnable(){
                    public void run(){
                        outputBuffer = new ByteArrayOutputStream();
                        outputBufferStream = new DataOutputStream(outputBuffer);

                        switch (mStartN) {
                        case 1:
                            mATline = String.format("%d,%d", engconstents.ENG_AT_SFPL, 0);
                            break;

                        case 2:
                            mATline = String.format("%d,%d", engconstents.ENG_AT_SEPL, 0);
                            break;
                        default:
                            mTextView.setText("ERROR");
                        }
                        Log.d(TAG, "mATline :" + mATline);
                        try {
                            outputBufferStream.writeBytes(mATline);
                        } catch (IOException e) {
                            Log.e(TAG, "writeBytes() error!");
                            mTextView.setText("ERROR");
                        }

                        mEf.engwrite(mSocketID,outputBuffer.toByteArray(),outputBuffer.toByteArray().length);

                        int dataSize = 512;
                        byte[] inputBytes = new byte[dataSize];

                        int showlen= mEf.engread(mSocketID, inputBytes, dataSize);
                        String mATResponse =  new String(inputBytes, 0, showlen);
                        if(mATResponse.length() >= 10){
                            mATResponse = mATResponse.substring(10);//delete "+SFPL: " line
                        }
                        Log.e(TAG, "mATResponse:" + mATResponse);
                        if (mATResponse.length() > 0) {
                            mTextView.setText(mATResponse);
                        } else {
                            mTextView.setText("NULL");
                        }
                    }
                }).start();
        /*Add 20130201 Spreadst of 123080 ANR after press mannual assert  end */
    }
}


