use std::fs::File;
use std::io::{self, Seek};
use std::io::SeekFrom;
use std::io::Read;
use core::ffi::c_char;
use std::ffi::CStr;
use zip::ZipArchive;
use sys_locale::get_locale;

const FT_NOMOREFIELDS: i32 = 0;
const FT_STRING: i32 = 8;

// for ContentGetValue
const FT_NOSUCHFIELD: i32 = -1;
const FT_FILEERROR: i32 = -2;
const FT_FIELDEMPTY: i32 = -3;

const FIELD_ARRAY: [(&str, &str, i32); 11] = [("Manifest version", "manifest_version", FT_STRING), ("Name", "name", FT_STRING), ("Version", "version", FT_STRING),
                                              ("Description", "description", FT_STRING), ("Author", "author", FT_STRING), ("Short name", "short_name", FT_STRING),
                                              ("Version name", "version_name", FT_STRING), ("Chrome version", "minimum_chrome_version", FT_STRING),
                                              ("Home page", "homepage_url", FT_STRING), ("Update URL", "update_url", FT_STRING),
                                              ("Permissions", "permissions", FT_STRING)];

static mut FILE_NAME: String = String::new();
static mut JSON: serde_json::Value = serde_json::Value::Null;
static mut MESSAGES: serde_json::Value = serde_json::Value::Null;

unsafe fn copy_rust_str_to_c_arr(s: &str, arr: *mut c_char, known_len: usize)
{
    let mut len: usize = s.len();
    if len + 1 >= known_len
    {
        len = known_len - 1;
    }
    unsafe {
        let our_ptr = s.as_bytes().as_ptr();
        std::ptr::copy_nonoverlapping(our_ptr, arr.cast(), len);
        *arr.add(len) = b'\0' as _;
    }
}

fn parse_locale(mut zip: ZipArchive<File>) -> io::Result<()>
{
    let value: Option<&serde_json::Value>;
    unsafe {
        value = JSON.get("default_locale");
    }
    match value
    {
        Some(val) =>
            {
                match val.as_str()
                {
                    Some(s) =>
                        {
                            const MSG: &str = "_locales/{}/messages.json";

                            let mut locale = get_locale().unwrap_or_else(|| s.to_string());
                            locale = locale.replace("-", "_");

                            let mut lng = MSG.replace("{}", &locale);
                            let mut res = zip.by_name(&lng);
                            if res.is_err()
                            {
                                std::mem::drop(res);
                                let loc: Vec<&str> = locale.split('_').collect();
                                lng = MSG.replace("{}", &loc[0]);
                                res = zip.by_name(&lng);
                                if res.is_err()
                                {
                                    std::mem::drop(res);
                                    lng = MSG.replace("{}", &s);
                                    res = zip.by_name(&lng);
                                }
                            }
                           if res.is_ok()
                           {
                               let file = res.unwrap();
                               unsafe {
                                   MESSAGES = serde_json::from_reader(file)?;
                               }
                           }
                        },
                    None => {}
                }
            },
        None => {}
    }
    Ok(())
}

fn parse(file_name: &String) -> io::Result<()>
{
    let mut offset: u32;
    let mut file = File::open(file_name)?;

    if file_name.to_lowercase().ends_with(".crx")
    {
        let mut buffer = [0; 4];

        file.read(&mut buffer[..])?;

        if (buffer[0] != 0x43) || (buffer[1] != 0x72) || (buffer[2] != 0x32)|| (buffer[3] != 0x34)
        {
            return Err(io::Error::from_raw_os_error(0));
        }

        file.read(&mut buffer[..])?;

        let version = u32::from_le_bytes(buffer);

        // println!("Version: {}",  version);

        if version == 3
        {
            file.read(&mut buffer[..])?;
            offset = u32::from_le_bytes(buffer);
        }
        else if version == 2
        {
            file.read(&mut buffer[..])?;
            offset = u32::from_le_bytes(buffer);
            file.read(&mut buffer[..])?;
            offset += u32::from_le_bytes(buffer);
        }
        else {
            return Err(io::Error::from_raw_os_error(0));
        }
        // println!("Offset: {}",  offset);

        file.seek(SeekFrom::Current(offset as i64))?;
    }

    let mut zip = zip::ZipArchive::new(file)?;

    let file = zip.by_name("manifest.json")?;

    // println!("File size: {}", file.size());

    unsafe {
        JSON = serde_json::from_reader(file)?;
    }
    let _ = parse_locale(zip);

    Ok(())
}

fn get_locale_str(str: String) -> String
{
    if str.starts_with("__MSG_") & str.ends_with("__")
    {
        unsafe {
            if MESSAGES == serde_json::Value::Null
            {
                return str;
            }
        }

        let mut ss: String = str.chars().skip(6).take(str.len() - 8).collect();

        if ss.len() == 0
        {
            return str;
        }
        ss.insert(0, '/');
        ss.push_str("/message");

        let value: Option<&serde_json::Value>;
        unsafe {
            value = MESSAGES.pointer(&ss);
        }

        match value
        {
            Some(val) =>
                {
                    match val.as_str()
                    {
                        Some(s) => s.to_string(),
                        None => str
                    }
                },
            None => str
        }
    } else {
        str
    }
}

#[no_mangle]
pub unsafe extern "system" fn ContentGetSupportedField(field_index: i32, field_name: *mut c_char, _units: *const c_char, maxlen: i32) -> i32
{
    let index: usize = field_index as usize;

    if (field_index < 0) || (index >= FIELD_ARRAY.len())
    {
        return FT_NOMOREFIELDS;
    }

    copy_rust_str_to_c_arr(&FIELD_ARRAY[index].0, field_name, maxlen as usize);

    FIELD_ARRAY[index].2
}

#[no_mangle]
pub unsafe extern "system" fn ContentGetValue(file_name: *const c_char, field_index: i32, _unit_index: i32, field_value: *mut c_char, maxlen: i32, _flags: i32) -> i32
{
    let index = field_index as usize;

    if (field_index < 0) || (index >= FIELD_ARRAY.len())
    {
        return FT_NOSUCHFIELD;
    }

    let value: String;
    let cstr = CStr::from_ptr(file_name);
    let filename = String::from_utf8_lossy(cstr.to_bytes()).to_string();

    if FILE_NAME != filename
    {
        FILE_NAME = filename;
        if parse(&FILE_NAME).is_err() {
            return FT_FILEERROR;
        }
    }
    let result: Option<&serde_json::Value>;
    unsafe {
        result = JSON.get(FIELD_ARRAY[index].1)
    }
    match result
    {
        Some(val) => value =
            {
                if val.is_array()
                {
                    val.to_string()
                }
                else if val.is_u64()
                {
                    match val.as_u64()
                    {
                        Some(u) => u.to_string(),
                        None => return FT_FIELDEMPTY,
                    }
                }
                else if val.is_string()
                {
                    match val.as_str()
                    {
                        Some(s) => get_locale_str(s.to_string()),
                        None => return FT_FIELDEMPTY,
                    }
                }
                else
                {
                    return FT_FIELDEMPTY;
                }
            },
        None => return FT_FIELDEMPTY
    }

    copy_rust_str_to_c_arr(&value, field_value, maxlen as usize);

    FIELD_ARRAY[index].2
}

#[no_mangle]
pub unsafe extern "system" fn ContentGetDetectString(detect_string: *mut c_char, maxlen: i32)
{
    copy_rust_str_to_c_arr("(EXT=\"CRX\") | (EXT=\"XPI\")", detect_string, maxlen as usize);
}
