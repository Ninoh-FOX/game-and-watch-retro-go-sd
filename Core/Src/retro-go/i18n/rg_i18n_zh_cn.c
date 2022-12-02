/*
***********************************************************
*                Warning!!!!!!!                           *
*  This file must be saved with GBK(or GB2312) Encoding   *
***********************************************************
*/
#if !defined (INCLUDED_ZH_CN)
#define INCLUDED_ZH_CN 0
#endif
#if !defined (CHEAT_CODES)
#define CHEAT_CODES 0
#endif
#if INCLUDED_ZH_CN==1

// Stand ��������

int zh_cn_fmt_Title_Date_Format(char *outstr, const char *datefmt, uint16_t day, uint16_t month, const char *weekday, uint16_t hour, uint16_t minutes, uint16_t seconds)
{
    return sprintf(outstr, datefmt, month, day, weekday, hour, minutes, seconds);
};

int zh_cn_fmt_Date(char *outstr, const char *datefmt, uint16_t day, uint16_t month, uint16_t year, const char *weekday)
{
    return sprintf(outstr, datefmt, year, month, day, weekday);
};

int zh_cn_fmt_Time(char *outstr, const char *timefmt, uint16_t hour, uint16_t minutes, uint16_t seconds)
{
    return sprintf(outstr, timefmt, hour, minutes, seconds);
};

const lang_t lang_zh_cn LANG_DATA = {
    .codepage = 936,
    .extra_font = cjk_zh_cn,
    .s_LangUI = "��������",
    .s_LangTitle = "��Ϸ����",
    .s_LangName = "S_Chinese",
    
    // Core\Src\porting\gb\main_gb.c =======================================
    .s_Palette = "��ɫ��",
    //=====================================================================

    // Core\Src\porting\nes\main_nes.c =====================================
    //.s_Palette= "��ɫ��" dul
    .s_Default = "Ĭ��",
    //=====================================================================

    // Core\Src\porting\gw\main_gw.c =======================================
    .s_copy_RTC_to_GW_time = "��ϵͳʱ��ͬ��",
    .s_copy_GW_time_to_RTC = "ͬ��ʱ�䵽ϵͳ",
    .s_LCD_filter = "��Ļ�����",
    .s_Display_RAM = "��ʾ�ڴ���Ϣ",
    .s_Press_ACL = "������Ϸ",
    .s_Press_TIME = "ģ�� TIME  �� [B+TIME]",
    .s_Press_ALARM = "ģ�� ALARM �� [B+GAME]",
    .s_filter_0_none = "��",
    .s_filter_1_medium = "��",
    .s_filter_2_high = "��",
    //=====================================================================

    // Core\Src\porting\odroid_overlay.c ===================================
    .s_Full = "\x7",
    .s_Fill = "\x8",

    .s_No_Cover = "�޷���",

    .s_Yes = "�� ��",
    .s_No = "�� ��",
    .s_PlsChose = "��ѡ��",
    .s_OK = "�� ȷ��",
    .s_Confirm = "��Ϣȷ��",
    .s_Brightness = "����",
    .s_Volume = "����",
    .s_OptionsTit = "ϵͳ����",
    .s_FPS = "֡��",
    .s_BUSY = "���أ�CPU��",
    .s_Scaling = "����",
    .s_SCalingOff = "�ر�",
    .s_SCalingFit = "����Ӧ",
    .s_SCalingFull = "ȫ��",
    .s_SCalingCustom = "�Զ���",
    .s_Filtering = "����",
    .s_FilteringNone = "��",
    .s_FilteringOff = "�ر�",
    .s_FilteringSharp = "����",
    .s_FilteringSoft = "���",
    .s_Speed = "�ٶ�",
    .s_Speed_Unit = "��",
    .s_Save_Cont = "�� �������",
    .s_Save_Quit = "�� �����˳�",
    .s_Reload = "�� ���¼���",
    .s_Options = "�� ��Ϸ����",
    .s_Power_off = "�� �ػ�����",
    .s_Quit_to_menu = "�� �˳���Ϸ",
    .s_Retro_Go_options = "��Ϸѡ��",

    .s_Font = "������ʽ",
    .s_Colors = "ɫ�ʷ���",

    .s_Theme_Title = "������ʽ",
    .s_Theme_sList = "���б�",
    .s_Theme_CoverV = "��ֱ����", // vertical
    .s_Theme_CoverH = "ˮƽ����", // horizontal
    .s_Theme_CoverLightV = "��ֱ����",
    .s_Theme_CoverLightH = "ˮƽ����",
    //=====================================================================

    // Core\Src\retro-go\rg_emulators.c ====================================

    .s_File = "���ƣ�",
    .s_Type = "���ͣ�",
    .s_Size = "��С��",
    .s_ImgSize = "ͼ��",
    .s_Close = "�� �ر�",
    .s_GameProp = "��Ϸ�ļ�����",
    .s_Resume_game = "�� ������Ϸ",
    .s_New_game = "�� ��ʼ��Ϸ",
    .s_Del_favorite = "�� �Ƴ��ղ�",
    .s_Add_favorite = "�� �����ղ�",
    .s_Delete_save = "�� ɾ������",
    .s_Confiem_del_save = "��ȷ��Ҫɾ���ѱ������Ϸ���ȣ�",
#if CHEAT_CODES == 1
    .s_Cheat_Codes = "�� ����ָ��",
    .s_Cheat_Codes_Title = "����ָ",
    .s_Cheat_Codes_ON = "\x6",
    .s_Cheat_Codes_OFF = "\x5",
#endif        

    //=====================================================================

    // Core\Src\retro-go\rg_main.c =========================================
    .s_Second_Unit = "��",
    .s_Version = "�桡������",
    .s_Author = "�ر��ף�",
    .s_Author_ = "����������",
    .s_UI_Mod = "����������",
    .s_Lang = "�������ģ�",
    .s_LangAuthor = "�ӽ�����",
    .s_Debug_menu = "�� ������Ϣ",
    .s_Reset_settings = "�� �����趨",
    //.s_Close                  = "Close",
    .s_Retro_Go = "���� Retro-Go",
    .s_Confirm_Reset_settings = "��ȷ��Ҫ���������趨��Ϣ��",

    .s_Flash_JEDEC_ID = "�洢 JEDEC ID",
    .s_Flash_Name = "�洢оƬ",
    .s_Flash_SR = "�洢״̬",
    .s_Flash_CR = "�洢����",
    .s_Smallest_erase = "��С��λ",
    .s_DBGMCU_IDCODE = "DBGMCU IDCODE",
    .s_Enable_DBGMCU_CK = "���� DBGMCU CK",
    .s_Disable_DBGMCU_CK = "���� DBGMCU CK",
    //.s_Close                  = "Close",
    .s_Debug_Title = "����ѡ��",
    .s_Idle_power_off = "���д���",
    .s_Splash_Option = "��������",
    .s_Splash_On = "\x6",
    .s_Splash_Off = "\x5",

    .s_Time = "ʱ�䣺",
    .s_Date = "���ڣ�",
    .s_Time_Title = "ʱ��",
    .s_Hour = "ʱ��",
    .s_Minute = "�֣�",
    .s_Second = "�룺",
    .s_Time_setup = "ʱ������",

    .s_Day = "��  ��",
    .s_Month = "��  ��",
    .s_Year = "��  ��",
    .s_Weekday = "���ڣ�",
    .s_Date_setup = "��������",

    .s_Weekday_Mon = "һ",
    .s_Weekday_Tue = "��",
    .s_Weekday_Wed = "��",
    .s_Weekday_Thu = "��",
    .s_Weekday_Fri = "��",
    .s_Weekday_Sat = "��",
    .s_Weekday_Sun = "��",

    .s_Date_Format = "20%02d��%02d��%02d�� ��%s",
    .s_Title_Date_Format = "%02d-%02d ��%s %02d:%02d:%02d",
    .s_Time_Format = "%02d:%02d:%02d",

    .fmt_Title_Date_Format = zh_cn_fmt_Title_Date_Format,
    .fmtDate = zh_cn_fmt_Date,
    .fmtTime = zh_cn_fmt_Time,
    //=====================================================================
};

#endif