#include "globals.h"
#ifdef READER_JET
#include "reader-common.h"
#include "cscrypt/des.h"
#include "cscrypt/jet_twofish.h"
#include <time.h>

#define CRC16 0x8005
static const uint8_t vendor_key[32] = {0x54, 0xF5, 0x53, 0x12, 0xEA, 0xD4, 0xEC, 0x03, 0x28, 0x60, 0x80, 0x94, 0xD6, 0xC4, 0x3A, 0x48, 
                                         0x43, 0x71, 0x28, 0x94, 0xF4, 0xE3, 0xAB, 0xC7, 0x36, 0x59, 0x17, 0x8E, 0xCC, 0x6D, 0xA0, 0x9B};

#define jet_write_cmd(reader, cmd, len, encrypt_tag, title) \
 do { \
	uint8_t cmd_buf[256];\
	uint8_t cmd_tmp[256];\
	memset(cmd_buf, 0, sizeof(cmd_buf));\
	memcpy(cmd_buf, cmd, len);\
	uint16_t crc=calc_crc16(cmd_buf, len);\
	cmd_buf[len] = crc >> 8;\
	cmd_buf[len + 1] = crc & 0xFF;\
	if(!jet_encrypt(reader, encrypt_tag, cmd_buf, len + 2, cmd_tmp, sizeof(cmd_tmp))){\
		rdr_log(reader, "error: %s failed... (encrypt cmd failed.)", title);\
		return ERROR;\
	}\
	cmd_tmp[4] += 2;\
	write_cmd(cmd_tmp, cmd_tmp + 5);\
	if(cta_res[cta_lr - 2] != 0x90 || cta_res[cta_lr - 1] != 0x00){\
		rdr_log(reader, "error: %s failed... ", title);\
		return ERROR;\
	}\
  } while (0)

#define jet_write_cmd_hold(reader, cmd, len, encrypt_tag, title) \
 do { \
	uint8_t cmd_buf[256];\
	uint8_t cmd_tmp[256];\
	memset(cmd_buf, 0, sizeof(cmd_buf));\
	memcpy(cmd_buf, cmd, len);\
	uint16_t crc=calc_crc16(cmd_buf, len);\
	cmd_buf[len] = crc >> 8;\
	cmd_buf[len + 1] = crc & 0xFF;\
	if(jet_encrypt(reader, encrypt_tag, cmd_buf, len + 2, cmd_tmp, sizeof(cmd_tmp))){\
		cmd_tmp[4] += 2;\
		write_cmd(cmd_tmp, cmd_tmp + 5);\
		if(cta_res[cta_lr - 2] != 0x90 || cta_res[cta_lr - 1] != 0x00){\
			rdr_log(reader, "error: %s failed... ", title);\
		}\
	}\
	else \
		rdr_log(reader, "error: %s failed... (encrypt cmd failed.)", title);\
  } while (0)

static uint16_t calc_crc16( const uint8_t *data, size_t size)
{
	uint16_t out = 0;
	int bits_read = 0, bit_flag;

	/* Sanity check: */
	if(data == NULL)
		return 0;

	while(size > 0)
	{
		bit_flag = out >> 15;

		/* Get next bit: */
		out <<= 1;
		out |= (*data >> (7 - bits_read)) & 1;

		/* Increment bit counter: */
		bits_read++;
		if(bits_read > 7)
		{
			bits_read = 0;
			data++;
			size--;
		}

		/* Cycle check: */
		if(bit_flag)
			out ^= CRC16;

	}
	return out;
}

static size_t jet_encrypt(struct s_reader* reader,uint8_t tag, uint8_t *data, size_t len, uint8_t *out, size_t maxlen)
{
	uint8_t buf[256];
	size_t i;
	size_t aligned_len = (len + 15) / 16 * 16;
	if((aligned_len + 7) > maxlen || (aligned_len + 7) > 256)
		return 0;
	memset(buf, 0, aligned_len + 7);

	out[0] = 0x84;
	out[1] = tag;
	out[2] = 0;
	out[3] = 0;
	out[4] = aligned_len & 0xFF;
	memcpy(buf, data, len);
	if(tag == 0x15){
		twofish(buf,len, out + 5,maxlen,reader->jet_vendor_key,sizeof(vendor_key),0);
	}
	else if(tag == 0x16){
		for(i = 0; i < (aligned_len / 8); i++)
			des_ecb_encrypt(buf + 8 * i, reader->jet_vendor_key + (i % 4) * 8, 8);
		memcpy(out + 5, buf, aligned_len);
	}
	out[aligned_len + 5] = 0x90;
	out[aligned_len + 6] = 0x00;

	return (aligned_len + 7);
}
#if 0
static size_t jet_decrypt(struct s_reader* reader, uint8_t *data,  uint8_t *out, size_t maxlen)
{
	uint8_t buf[256];
	size_t i;
	uint8_t tag;
	int len = data[4];

	memset(buf, 0, sizeof(buf));
	memset(out, 0, maxlen);
	tag = data[1];

	memcpy(buf, data + 5, len);
	if(tag == 0x15){
		twofish(buf,len, out,maxlen,reader->jet_vendor_key,sizeof(vendor_key),1);
	}
	else if(tag == 0x16){
		for(i = 0; i < (len / 8); i++)
			des_ecb_encrypt(buf + 8 * i, reader->vendor_key + (i % 4) * 8, 8);
		memcpy(out, buf, len);
	}
	else
		memcpy(out, buf, len);
	return (len);
}
#endif

/*================================================================*/

static int32_t cw_is_valid(unsigned char *cw) //returns 1 if cw_is_valid, returns 0 if cw is all zeros
{
	int32_t i;

	for(i = 0; i < 16; i++)
	{
		if(cw[i] != 0)  //test if cw = 00
		{
			return OK;
		}
	}
	return ERROR;
}

static int generate_derivekey(struct s_reader *reader, uint8_t * out, int len)
{
	uint8_t mask_key[32] = {0x16,0x23,0x6A,0x8A,0xF5,0xC2,0x8E,0x6,0x14,0x53,0xCF,0x6E,0x12,0xA1,0x2E,0xC5,
				0xE4,0xF8,0x94,0x10,0x03,0x0A,0xD8,0xC6,0xD4,0x55,0xE8,0x4A,0xB6,0x22,0x09,0xAD};
	uint8_t temp[128];
	uint8_t derivekey[56]={0x59, 0x32, 0x00, 0x00};
	int i;

	if(len < (int)sizeof(derivekey))
		return 0;
	memset(temp, 0 , sizeof(temp));
	memcpy(temp + 56, mask_key, sizeof(mask_key));
	memcpy(temp + 56 + sizeof(mask_key), reader->hexserial, 8);
	memcpy(temp + 56 + sizeof(mask_key) + 8, reader->jet_root_key, 8);
	temp[12] = temp[100] ^ temp[92];
	temp[16] = temp[102] ^ temp[94];
	temp[20] = temp[97];
	temp[32] = temp[96] ^ temp[88];
	temp[36] = temp[97] ^ temp[89];
	temp[40] = temp[98] ^ temp[90];
	temp[44] = temp[99] ^ temp[91];
	temp[48] = temp[101] ^ temp[93];
	temp[52] = temp[103];

	memcpy(derivekey + 4, reader->jet_root_key, 8);
	derivekey[12] = temp[32];
	derivekey[13] = temp[36];
	derivekey[14] = temp[40];
	derivekey[15] = temp[44];
	derivekey[16] = temp[12];
	derivekey[17] = temp[48];
	derivekey[18] = temp[16];
	derivekey[19] = temp[52] ^ temp[95];

	for(i = 0; i < 36; i++)
		derivekey[20 + i] = temp[54 + i] ^ temp[ (i % 8) + 96];
	uint16_t crc = calc_crc16(derivekey, 54);
	derivekey[54] = crc >> 8;
	derivekey[55] = crc & 0xFF;
	memcpy(out, derivekey, sizeof(derivekey));
	return sizeof(derivekey);
}

static int32_t jet_card_init(struct s_reader *reader, ATR *newatr)
{
	uint8_t get_serial_cmd[37] = {0x21, 0x21, 0x00, 0x00, 0x00};
	uint8_t get_key_cmd[6] = {0x58, 0x02, 0x00, 0x00, 0x00, 0x00};
	uint8_t register_key_cmd[48] = {0x15, 0x2C, 0x00, 0x00};
	uint8_t change_vendorkey_cmd[12] = {0x12, 0x08, 0x00, 0x00};
	uint8_t pairing_cmd01[38] = {0x20, 0x22, 0x00, 0x00};
	uint8_t pairing_cmd02[53] = {0x37, 0x31, 0x00, 0x00};
	uint8_t confirm_box_cmd[55] = {0x93, 0x33, 0x00, 0x00, 0x00, 0x00};
	uint8_t unknow_cmd1[39] = {0x71, 0x23, 0x00, 0x00, 0x04, 0x00};

	get_atr;
	def_resp;
	uint8_t cmd_buf[256];
	uint8_t temp[256];
	uint8_t buf[256];
	int i;
	struct twofish_ctx ctx;

	if((atr_size != 20) || atr[0] != 0x3B || atr[1] != 0x7F) { return ERROR; }
	if(atr[17] > 0x34 && atr[18] > 0x32)
		reader->cas_version=5;
	else
		reader->cas_version=1;

	rdr_log(reader, "Jet card detect");
	reader->caid = 0x4A30;
	reader->nprov = 1;

	if(reader->cas_version != 5){
		rdr_log(reader, "Only support jet version 5 !");
		return ERROR;
	}

	memset(reader->prid, 0x00, sizeof(reader->prid));
	memcpy(reader->jet_vendor_key, vendor_key, sizeof(vendor_key));

	// get serial step1
	jet_write_cmd(reader, get_serial_cmd, sizeof(get_serial_cmd), 0xAA, "get serial step1");
	memcpy(reader->hexserial, cta_res + 9, 8);

	//get root key
	jet_write_cmd(reader, get_key_cmd, sizeof(get_key_cmd), 0xAA, "get rootkey");
	memcpy(temp, cta_res + 5, cta_res[4]);
	memset(temp + cta_res[4], 0, sizeof(temp) - cta_res[4]);
	twofish_setkey(&ctx, reader->jet_vendor_key, sizeof(reader->jet_vendor_key));
	twofish_decrypt(&ctx, temp, cta_res[4], buf, sizeof(buf));		//twfish decrypt
	memset(reader->jet_root_key, 0 ,sizeof(reader->jet_root_key));
	memcpy(reader->jet_root_key, buf + 4, (cta_res[4] < sizeof(reader->jet_root_key)) ? cta_res[4] : sizeof(reader->jet_root_key));

	//get derive key
	memset(cmd_buf, 0, sizeof(cmd_buf));
	if(!generate_derivekey(reader, cmd_buf, sizeof(cmd_buf))){
		rdr_log(reader, "error: generate derivekey faild, buffer overflow!");
		return ERROR;
	}
	//generate_derivekey has filled crc16. so call jet_write_cmd with len - 2.
	jet_write_cmd(reader, cmd_buf, sizeof(reader->jet_derive_key) - 2, 0xAA, "get derivekey");
	memcpy(reader->jet_derive_key, cmd_buf, sizeof(reader->jet_derive_key));

	//get auth key
	memset(cmd_buf, 0, sizeof(cmd_buf));
	memcpy(cmd_buf, get_key_cmd, sizeof(get_key_cmd));
	memcpy(cmd_buf + 4, reader->jet_root_key, 2);
	jet_write_cmd(reader, cmd_buf, sizeof(get_key_cmd), 0xAA, "get authkey");
	memset(temp, 0, sizeof(temp));
	memcpy(temp, cta_res, cta_res[4]);
	twofish_decrypt(&ctx, temp, cta_res[4], buf, sizeof(buf));		//twfish decrypt
	memcpy(reader->jet_auth_key, buf, 10);

	//confirm auth key
	memcpy(register_key_cmd + 36, reader->jet_auth_key, 8);
	memcpy(register_key_cmd + 44, reader->jet_derive_key, 4);
	jet_write_cmd(reader, register_key_cmd, sizeof(register_key_cmd), 0x15, "confirm auth");

	//change vendor key
	jet_write_cmd(reader, change_vendorkey_cmd, sizeof(change_vendorkey_cmd), 0x15, "change vendorkey");
	memset(temp, 0, sizeof(temp));
	memcpy(temp, cta_res + 5, cta_res[4]);
	twofish_decrypt(&ctx, temp, cta_res[4], buf, sizeof(buf));		//twfish decrypt
	if(((cta_res[4] + 15)/ 16 * 16) == 48 && buf[0] == 0x42 && buf[1] == 0x20)
		memcpy(reader->jet_vendor_key, buf + 4, 32);

	//pairing step1
	if(reader->boxkey_length)
		memcpy(pairing_cmd01 + 4, reader->boxkey, 32);
	pairing_cmd01[37] = 0x01;
	jet_write_cmd_hold(reader, pairing_cmd01, sizeof(pairing_cmd01), 0x15, "pairing step 1");
	memset(temp, 0, sizeof(temp));
	memcpy(temp, cta_res + 5, cta_res[4]);
	twofish_decrypt(&ctx, temp, cta_res[4], buf, sizeof(buf));		//twfish decrypt
	if(buf[0] != 0x41)
		rdr_log(reader, "error: pairing step 1 failed(invalid data)! continue ...");

	//pairing step 2
	if(reader->boxkey_length)
		memcpy(pairing_cmd02 + 4, reader->boxkey, 32);
	pairing_cmd02[36] = 0x01;
	for(i = 37;i < 45; i++)
		pairing_cmd02[i] = 0x30;
	memcpy(pairing_cmd02 + 45, reader->jet_derive_key + 45, 8);
	jet_write_cmd_hold(reader, pairing_cmd02, sizeof(pairing_cmd02), 0x15, "pairing step 2");

	//get service key
	if(reader->boxkey_length)
		memcpy(get_key_cmd + 4, reader->boxkey, 2);
	jet_write_cmd(reader, get_key_cmd, sizeof(get_key_cmd), 0xAA, "get service key");
	memset(temp, 0, sizeof(temp));
	memcpy(temp, cta_res + 5, cta_res[4]);
	twofish_decrypt(&ctx, temp, cta_res[4], buf, sizeof(buf));		//twfish decrypt
	memcpy(reader->jet_service_key, buf + 4, 8);
	reader->jet_service_key[3] += reader->jet_service_key[1];

	//register service key
	if(reader->boxkey_length)
		memcpy(register_key_cmd + 4, reader->boxkey, 32);
	memcpy(register_key_cmd + 36, reader->jet_service_key, 8);
	memcpy(register_key_cmd + 44, reader->jet_derive_key + 44, 4);
	register_key_cmd[44] = 0x30;
	jet_write_cmd(reader, register_key_cmd, sizeof(register_key_cmd), 0x15, "register service key");

	//confirm box 1
	if(reader->boxkey_length)
		memcpy(confirm_box_cmd + 6, reader->boxkey, 32);
	confirm_box_cmd[38] = 0x01;
	for(i = 39;i < 47; i++)
		confirm_box_cmd[i] = 0x30;
	memcpy(confirm_box_cmd + 47, reader->jet_derive_key + 47, 8);
	jet_write_cmd(reader, confirm_box_cmd, sizeof(confirm_box_cmd), 0x15, "confirm box step 1");

	//unknow cmd 1
	if(reader->boxkey_length)
		memcpy(unknow_cmd1 + 7, reader->boxkey, 32);
	jet_write_cmd_hold(reader, unknow_cmd1, sizeof(unknow_cmd1), 0x15, "unknow_cmd1");

	//get serial step2
	get_serial_cmd[4] = 0x01;
	jet_write_cmd(reader, get_serial_cmd, sizeof(get_serial_cmd), 0xAA, "get serial step 2");
	memcpy(reader->hexserial, cta_res + 5, 8);

	//confirm box 2
	confirm_box_cmd[4] = 0x10;
	if(reader->boxkey_length)
		memcpy(confirm_box_cmd + 6, reader->boxkey, 32);
	confirm_box_cmd[38] = 0x01;
	for(i = 39;i < 47; i++)
		confirm_box_cmd[i] = 0x30;
	memcpy(confirm_box_cmd + 47, reader->jet_derive_key + 47, 8);
	jet_write_cmd(reader, confirm_box_cmd, sizeof(confirm_box_cmd), 0x15, "confirm box step 2");

	rdr_log_sensitive(reader, "type: jet, caid: %04X, serial: %llu, hex serial: %08llX, boxkey: %s",
			reader->caid, (uint64_t) b2ll(8, reader->hexserial), (uint64_t) b2ll(8, reader->hexserial),
			cs_hexdump(0, reader->boxkey, 32, (char*)buf, sizeof(buf)));

	return OK;
}


static int32_t jet_do_ecm(struct s_reader *reader, const ECM_REQUEST *er, struct s_ecm_answer *ea)
{
	uint8_t cmd[256] = {0x00, 0xB2, 0x00, 0x00};
	uint8_t temp[256] = {0};
	uint8_t ecm[512] = {0};

	int i, off, len;
	int ecm_len;
	char * tmp;

	def_resp;

	if(cs_malloc(&tmp, er->ecmlen * 3 + 1))
	{
		rdr_log_dbg(reader, D_IFD, "ECM: %s", cs_hexdump(1, er->ecm, er->ecmlen, tmp, er->ecmlen * 3 + 1));
		NULLFREE(tmp);
	}
	if((ecm_len = check_sct_len(er->ecm, 3, sizeof(er->ecm))) < 0) {
		rdr_log(reader, "error: check_sct_len failed, smartcard section too long %d > %d", SCT_LEN(er->ecm), sizeof(er->ecm) - 3);
		return ERROR;
	}

	memcpy(ecm, er->ecm, ecm_len);
	len = ((ecm[1] & 0x0F) << 8) + ecm[2];
	if(len < 0x8A){
		rdr_log(reader, "error: invalid ecm data...");
		return ERROR;
	}

	i = 0;
	if(ecm[2] == 0x8B)
		i = -2;
	off = len + 3 - (i + 12) - 4;
	if(ecm[2] == 0x9E){
		ecm[23] = ecm[23] ^ ecm[80] ^ ecm[90] ^ ecm[140];
		ecm[29] = ecm[29] ^ 0x59;
		ecm[41] = ecm[41] ^ 0xEA;
		off = 0x80;
	}
	len = off + 0x36;

	if(ecm[i + 8] == 4)
		cmd[0] = 0x1F;
	else if(ecm[i + 8] == 3)
		cmd[0] = 0x1E;
	else
		cmd[0] = ((ecm[i + 8] & 0x7F) == 4 && ecm[2] == 0x9E) ? 0x1F : 0x1B;
	memcpy(cmd + 4, ecm + i + 12, off);
	memcpy(cmd + 4 + off, reader->boxkey, 32);
	cmd[off + 36] = ecm[i + 10] ^ ecm[i + 138];
	cmd[off + 37] = ecm[i + 11] ^ ecm[i + 139];
	memcpy(cmd + off + 38, reader->jet_service_key, 8);
	jet_write_cmd(reader, cmd, len, 0x16, "parse ecm");
	if(cta_lr < 29){
			rdr_log(reader, "error: get cw failed...(response data too short.)");
			return ERROR;
	}

	memset(temp, 0, sizeof(temp));
	memcpy(temp, cta_res, cta_res[4] + 5);
	for(i = 0; i < (cta_res[4] / 8); i++)
		des_ecb_encrypt(temp + 5 + 8 * i, reader->jet_vendor_key + (i % 4) * 8, 8);
	if(temp[9] == 0xFF){
		rdr_log(reader, "error: invalid cw data... (cw[9]=0xFF)");
		return ERROR;
	}
	memcpy(ea->cw, temp + 11, 16);
	if(ERROR == cw_is_valid(ea->cw)){
		rdr_log(reader, "error: invalid cw data... (all zero)");
		return ERROR;
	}

	return OK;
}

static int32_t jet_get_emm_type(EMM_PACKET *ep, struct s_reader *UNUSED(reader))
{
	ep->type = UNKNOWN;
	return 1;
}

static int32_t jet_do_emm(struct s_reader *reader, EMM_PACKET *ep)
{
	uint8_t cmd[256] = { 0x1A, 0xB2, 0x00, 0x00};

	int i, len;
	def_resp;

	len = ((ep->emm[1] & 0x0F) << 8) + ep->emm[2];
	if(len < 148){
		rdr_log(reader, "error: emm data too short,(%d) < 148 ...", len);
		return ERROR;
	}

	if(ep->emm[10] != reader->hexserial[7]){
		rdr_log(reader, "error: do emm failed, card not match...");
		return ERROR;
	}

	len -= 4;
	i = len + 54;
	memcpy(cmd + 4, ep->emm + 17, len);
	memcpy(cmd + 4 + len, reader->boxkey, 32);
	memcpy(cmd + len + 40,ep->emm + 13, 4);
	cmd[len + 44] = 0x14;
	cmd[len + 46] = 0x01;
	cmd[len + 47] = 0x01;
	cmd[len + 52] = ep->emm[17] ^ ep->emm[145];
	cmd[len + 53] = ep->emm[144] ^ ep->emm[146];
	jet_write_cmd(reader, cmd, len + 54, 0x15, "parse emm");

	return OK;
}

static int32_t jet_card_info(struct s_reader *UNUSED(reader))
{

	return OK;
}

const struct s_cardsystem reader_jet =
{
	.desc         = "jet",
	.caids        = (uint16_t[]){ 0x4A, 0 },
	.do_emm       = jet_do_emm,
	.do_ecm       = jet_do_ecm,
	.card_info    = jet_card_info,
	.card_init    = jet_card_init,
	.get_emm_type = jet_get_emm_type,
};

#endif