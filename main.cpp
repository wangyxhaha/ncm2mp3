#include <iostream>
#include <fstream>
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <ctime>
#include <string>
#include <windows.h>
#include <memory>
#include <filesystem>
#include "json.hpp"
using namespace std;
using json=nlohmann::json;

bool ncm_crypt(string dir){
    string file_name;
    char magic_number[8]; // 开头的魔法数字，用来判断文件类型，为CTENFDAM
    char skip[2]; // 空两字节
    int length_of_RC4_key; // RC4密钥长度（解密前）
    int true_length_of_RC4_key; // RC4密钥长度（解密去填充后）
    unique_ptr<char> RC4_key; // RC4算法密钥
    int length_of_meta_infor; // 元数据长度（解密前）
    int true_length_of_meta_infor; // 元数据长度（解密去填充后）
    unique_ptr<char> meta_infor; // 元数据
    json meta_infor_json; // 元数据json格式解析
    int cover_crc; // 封面crc（不用管）
    char skip2[5]; //空五字节
    int length_of_cover_data; // 封面数据长度
    unique_ptr<char> cover_data; // 封面数据

    unsigned char meta_key[]={0x23,0x31,0x34,0x6C,0x6A,0x6B,0x5F,0x21,0x5C,0x5D,0x26,0x30,0x55,0x3C,0x27,0x28}; // 元数据密钥
	unsigned char key_of_RC4_key[]={0x68,0x7A,0x48,0x52,0x41,0x6D,0x73,0x6F,0x35,0x6B,0x49,0x6E,0x62,0x61,0x78,0x57}; // RC4密钥的密钥

    int name_pos=dir.find_last_of("/\\");
    if (name_pos==string::npos) name_pos=0;
    else name_pos++;
    int ncm_pos=dir.rfind(".ncm");
    if (ncm_pos==-1){
        cerr << "wrong file extension: " << dir << '\n';
        return false;
    }
    file_name=dir.substr(name_pos,ncm_pos-name_pos);
    ifstream fin(dir,ios::binary);
    if (!fin.good()){
        cerr << "unable to open file: " << '\"' << dir << '\"' << '\n';
        return false;
    }


    fin.read(magic_number,8); // 读取魔法数字
    fin.read(skip,2); // 跳过两字节

    /*解密RC4密钥*/
    fin.read((char *)(&length_of_RC4_key),4); // 读取RC4密钥长度
    unique_ptr<char> RC4_key_temp1(new char[length_of_RC4_key]);
    unique_ptr<char> RC4_key_temp2(new char[length_of_RC4_key]);

    fin.read(RC4_key_temp1.get(),length_of_RC4_key); // 读取RC4密钥
    for (int i=0;i<128;i++) *(RC4_key_temp1.get()+i)^=0x64; // 异或0x64

    AES_KEY decrypt_key1;
    AES_set_decrypt_key((unsigned char *)key_of_RC4_key,128,&decrypt_key1); // 设置解密密钥

    for (int i=0;i<128;i+=16){ // 进行解密
        AES_ecb_encrypt((unsigned char *)(RC4_key_temp1.get()+i),(unsigned char *)(RC4_key_temp2.get()+i),&decrypt_key1,AES_DECRYPT);
    }

    true_length_of_RC4_key=length_of_RC4_key-17-(int)*(RC4_key_temp2.get()+length_of_RC4_key-1); // 去填充以及标识
    // RC4_key=new char[true_length_of_RC4_key];
    RC4_key.reset(new char[true_length_of_RC4_key]);
    memcpy(RC4_key.get(),RC4_key_temp2.get()+17,true_length_of_RC4_key);

    /*获取元数据*/
    fin.read((char *)(&length_of_meta_infor),4);
    unique_ptr<char> meta_infor_temp1(new char[length_of_meta_infor]);
    // 曾经有过meta_infor_temp2，但是没了
    unique_ptr<char> meta_infor_temp3(new char[length_of_meta_infor]);
    unique_ptr<char> meta_infor_temp4(new char[length_of_meta_infor]);

    fin.read(meta_infor_temp1.get(),length_of_meta_infor);
    for (int i=0;i<length_of_meta_infor;i++) *(meta_infor_temp1.get()+i)^=0x63;

    int padding; // 去填充
    if (*(meta_infor_temp1.get()+length_of_meta_infor-2)=='=') padding=2;
    else if (*(meta_infor_temp1.get()+length_of_meta_infor-1)=='=') padding=1;
    else padding=0;
    int length_after_base64_decoding=EVP_DecodeBlock((unsigned char *)meta_infor_temp3.get(),(unsigned char *)meta_infor_temp1.get()+22,length_of_meta_infor-22)-padding; // base64解码

    AES_KEY decrypt_key2;
    AES_set_decrypt_key((unsigned char *)meta_key,128,&decrypt_key2);

    for (int i=0;i<length_after_base64_decoding;i+=16){ // 进行解密
        AES_ecb_encrypt((unsigned char *)(meta_infor_temp3.get()+i),(unsigned char *)(meta_infor_temp4.get()+i),&decrypt_key2,AES_DECRYPT);
    }

    true_length_of_meta_infor=length_after_base64_decoding-(int)*(meta_infor_temp4.get()+length_after_base64_decoding-1); // 去填充
    meta_infor.reset(new char[true_length_of_meta_infor+1]);
    memcpy(meta_infor.get(),meta_infor_temp4.get(),true_length_of_meta_infor);

    *(meta_infor.get()+true_length_of_meta_infor)='\0'; // 解析json
    meta_infor_json=json::parse(meta_infor.get()+6); // 要去掉开头的"music:"

    /*获取封面*/
    fin.read((char*)&cover_crc,4); // 获取crc
    fin.read(skip2,5); //跳过
    
    fin.read((char*)&length_of_cover_data,4);
    cover_data.reset(new char[length_of_cover_data]);
    fin.read(cover_data.get(),length_of_cover_data);

    ofstream cover_out("cover.jfif",ios::binary);
    cover_out.write(cover_data.get(),length_of_cover_data); // 输出封面图片
    cover_out.close();

    /*获取音频数据*/
    unsigned char RC4_Sbox[256];
    unsigned char RC4_Kbox[256];
    for (int i=0;i<256;i++) RC4_Sbox[i]=i; // 初始化RC4的S盒
    for (int i=0;i<256;i++) RC4_Kbox[i]=*(RC4_key.get()+i%true_length_of_RC4_key); // 初始化RC4的K盒

    for (int i=0,j=0;i<256;i++){ // 置换S盒
        j=(j+RC4_Sbox[i]+RC4_Kbox[i])&0xff;
        swap(RC4_Sbox[i],RC4_Sbox[j]);
    }

    try{
        filesystem::path output_path=filesystem::path(dir).parent_path()/"output";
        if (!filesystem::exists(output_path) && !filesystem::create_directories(output_path)){
            cerr << "fail to create directory\n";
            return false;
        }
    }
    catch(const filesystem::filesystem_error &e){
        cout << dir << endl;
        cout << e.path1() << ' ' << e.path2() << endl;
        cout << e.what() << endl;
    }

    string output_name=file_name+'.'+meta_infor_json["format"].get<string>();
    ofstream music_out("./output/"+output_name,ios::binary);

    if (!music_out.good()){
        cerr << "audio output error\n";
        return false;
    }

    constexpr int block_capacity=1024*1024; // 分块解密，块大小为1mb
    char music_data_block[block_capacity];
    int read_count;
    int length_of_music_data=0;
    auto start_time=chrono::high_resolution_clock::now();
    do{ // 非标准RC4解密并输出
        fin.read(music_data_block,block_capacity);
        read_count=fin.gcount();
        for (int i=0,j;i<read_count;i++){
            j=(i+length_of_music_data+1)&0xff;
            music_data_block[i]^=RC4_Sbox[(RC4_Sbox[j]+RC4_Sbox[(j+RC4_Sbox[j])&0xff])&0xff];
        }
        length_of_music_data+=read_count;
        music_out.write(music_data_block,read_count);
        if (fin.eof()) break;
    }
    while (read_count==block_capacity);
    music_out.close();

    auto end_time=chrono::high_resolution_clock::now();
    auto duration=std::chrono::duration_cast<std::chrono::milliseconds>(end_time-start_time);

    cout << "output: " << output_name << endl;
    cout << "song name: " << meta_infor_json["musicName"].get<string>() << endl;
    cout << "album: " << meta_infor_json["album"].get<string>() << endl;
    cout << "artist: ";
    for (int i=0;i<meta_infor_json["artist"].size();i++){
        cout << meta_infor_json["artist"][i][0].get<string>();
        if (i!=meta_infor_json["artist"].size()-1) cout << ", ";
    }
    cout << endl;
    cout << "total music data: " << double(length_of_music_data)/1024 << "KB" << endl;
    cout << "totally used time: " << duration.count() << "ms" << endl;

    return true;
}

int main(int argc,char *argv[]){
    SetConsoleOutputCP(CP_UTF8);
    if (argc>1){
        int success=0;
        for (int i=1;i<argc;i++){
            wcout << argv[i] << endl;
            if (ncm_crypt(string(argv[i]))) success++;
        }
        cout << "total tasks: " << argc-1 << endl;
        cout << "tasks succeed: " << success << endl;
    }
    else{
        string file;
        getline(cin,file);
        ncm_crypt(file);
    }
    system("pause");
    return 0;
}
 