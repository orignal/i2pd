#include <string.h>
#include "util.h"
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include "Log.h"
#include "Crypto.h"
#include "Family.h"

namespace i2p
{
namespace data
{
	Families::Families ()
	{
	}

	Families::~Families ()
	{
	}

	void Families::LoadCertificate (const std::string& filename)
	{
		SSL_CTX * ctx = SSL_CTX_new (TLSv1_method ());
		int ret = SSL_CTX_use_certificate_file (ctx, filename.c_str (), SSL_FILETYPE_PEM); 
		if (ret)
		{	
			SSL * ssl = SSL_new (ctx);
			X509 * cert = SSL_get_certificate (ssl);
			if (cert)
			{	
				std::shared_ptr<i2p::crypto::Verifier> verifier;
				// extract issuer name
				char name[100];
				X509_NAME_oneline (X509_get_issuer_name(cert), name, 100);
				char * cn = strstr (name, "CN=");
				if (cn)
				{	
					cn += 3;
					char * family = strstr (cn, ".family");
					if (family) family[0] = 0;
				}	
				auto pkey = X509_get_pubkey (cert);
				int keyType = EVP_PKEY_type(pkey->type);
				switch (keyType)
				{
					case EVP_PKEY_DSA:
						// TODO:
					break;
					case EVP_PKEY_EC:
					{
						EC_KEY * ecKey = EVP_PKEY_get1_EC_KEY (pkey);
						if (ecKey)
						{
							auto group = EC_KEY_get0_group (ecKey);
							if (group)
							{
								int curve = EC_GROUP_get_curve_name (group);
								if (curve == NID_X9_62_prime256v1)
								{
									uint8_t signingKey[64];
									BIGNUM * x = BN_new(), * y = BN_new();
									EC_POINT_get_affine_coordinates_GFp (group,
										EC_KEY_get0_public_key (ecKey), x, y, NULL);
									i2p::crypto::bn2buf (x, signingKey, 32);
									i2p::crypto::bn2buf (y, signingKey + 32, 32);
									BN_free (x); BN_free (y);
									verifier = std::make_shared<i2p::crypto::ECDSAP256Verifier>(signingKey);
								}	
								else
									LogPrint (eLogWarning, "Family: elliptic curve ", curve, " is not supported");
							}
							EC_KEY_free (ecKey);
						}
						break;
					}
					default:
						LogPrint (eLogWarning, "Family: Certificate key type ", keyType, " is not supported");
				}
				EVP_PKEY_free (pkey);
				if (verifier && cn)
					m_SigningKeys[cn] = verifier;
			}	
			SSL_free (ssl);			
		}	
		else
			LogPrint (eLogError, "Family: Can't open certificate file ", filename);
		SSL_CTX_free (ctx);		
	}

	void Families::LoadCertificates ()
	{
		boost::filesystem::path familyDir = i2p::util::filesystem::GetCertificatesDir() / "family";
		
		if (!boost::filesystem::exists (familyDir)) return;
		int numCertificates = 0;
		boost::filesystem::directory_iterator end; // empty
		for (boost::filesystem::directory_iterator it (familyDir); it != end; ++it)
		{
			if (boost::filesystem::is_regular_file (it->status()) && it->path ().extension () == ".crt")
			{	
				LoadCertificate (it->path ().string ());
				numCertificates++;
			}	
		}	
		if (numCertificates > 0)
			LogPrint (eLogInfo, "Family: ", numCertificates, " certificates loaded");
	}

	bool Families::VerifyFamily (const std::string& family, const IdentHash& ident, 
		const char * signature, const char * key)
	{
		uint8_t buf[50], signatureBuf[64];
		size_t len = family.length (), signatureLen = strlen (signature);
		memcpy (buf, family.c_str (), len);
		memcpy (buf + len, (const uint8_t *)ident, 32);
		len += 32;	
		Base64ToByteStream (signature, signatureLen, signatureBuf, 64);	
		auto it = m_SigningKeys.find (family);
		if (it != m_SigningKeys.end ())
			return it->second->Verify (buf, len, signatureBuf);
		// TODO: process key
		return true;
	}

	std::string CreateFamilySignature (const std::string& family, const IdentHash& ident)
	{
		std::string sig;
		auto filename = i2p::util::filesystem::GetDefaultDataDir() / "family" / (family + ".key");	
		SSL_CTX * ctx = SSL_CTX_new (TLSv1_method ());
		int ret = SSL_CTX_use_PrivateKey_file (ctx, filename.string ().c_str (), SSL_FILETYPE_PEM); 
		if (ret)
		{
			SSL * ssl = SSL_new (ctx);
			EVP_PKEY * pkey = SSL_get_privatekey (ssl);
			EC_KEY * ecKey = EVP_PKEY_get1_EC_KEY (pkey);
			if (ecKey)
			{
				auto group = EC_KEY_get0_group (ecKey);
				if (group)
				{
					int curve = EC_GROUP_get_curve_name (group);
					if (curve == NID_X9_62_prime256v1)
					{
						uint8_t signingPrivateKey[32], buf[50], signature[64];
						i2p::crypto::bn2buf (EC_KEY_get0_private_key (ecKey), signingPrivateKey, 32);
						i2p::crypto::ECDSAP256Signer signer (signingPrivateKey);
						size_t len = family.length ();
						memcpy (buf, family.c_str (), len);
						memcpy (buf + len, (const uint8_t *)ident, 32);
						len += 32;
						signer.Sign (buf, len, signature);
						len = Base64EncodingBufferSize (64);
						char * b64 = new char[len+1];
						len = ByteStreamToBase64 (signature, 64, b64, len);
						b64[len] = 0;
						sig = b64;
						delete[] b64;
					}
					else
						LogPrint (eLogWarning, "Family: elliptic curve ", curve, " is not supported");
				}	
			}	
			SSL_free (ssl);		
		}	
		else
			LogPrint (eLogError, "Family: Can't open keys file ", filename.string ());
		SSL_CTX_free (ctx);	
		return sig;
	}	
}
}

